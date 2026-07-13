#include "auto_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace predeye {
namespace {

// Prog jasnosci ramki slotu — z pomiarow RLE na realnych zrzutach: wnetrza
// pustych slotow i przerwy miedzy slotami < 45 luma, linie ramek >= 45.
constexpr int kBrightMin = 45;

// Dopuszczalne geometrie wzoru, skalowane wysokoscia ekranu (baza 1080p:
// odcinek ramki ~52-56 px, przerwa 4-8 px, krok 60 px; przerwa za crestem
// 18+ px CELOWO wypada poza zakres — crest nie nalezy do siatki itemow).
struct Ranges {
    int seg_min, seg_max;
    int gap_min, gap_max;
    int period_min, period_max;
    int min_cols;
    int y_glue;    // maks. przerwa w y wewnatrz jednego wiersza
    int x_group;   // tolerancja grupowania linii po x0
};

Ranges ranges_for(int height) {
    const double s = height / 1080.0;
    Ranges r;
    r.seg_min = static_cast<int>(40 * s);
    r.seg_max = static_cast<int>(64 * s);
    r.gap_min = std::max(2, static_cast<int>(3 * s));
    r.gap_max = static_cast<int>(14 * s);
    r.period_min = static_cast<int>(50 * s);
    r.period_max = static_cast<int>(74 * s);
    r.min_cols = 4;
    r.y_glue = std::max(3, static_cast<int>(6 * s));
    r.x_group = std::max(4, static_cast<int>(5 * s));
    return r;
}

// Jedna linia wzoru slotow: y, poczatek pierwszego odcinka, liczba slotow,
// krok i szerokosc odcinka.
struct PatternLine {
    int y = 0, x0 = 0, cols = 0, period = 0, seg = 0;
};

int median_of(std::vector<int>& v) {
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

// Znajdz na linii y okresowe lancuchy jasnych odcinkow (>= min_cols slotow).
void detect_at(const cv::Mat& gray, int y, const Ranges& rg, std::vector<PatternLine>& out) {
    struct Run {
        bool bright;
        int start, len;
    };
    const uchar* row = gray.ptr<uchar>(y);
    const int w = gray.cols;
    std::vector<Run> runs;
    int start = 0;
    bool bright = row[0] >= kBrightMin;
    for (int x = 1; x <= w; ++x) {
        const bool b = x < w && row[x] >= kBrightMin;
        if (x == w || b != bright) {
            runs.push_back({bright, start, x - start});
            start = x;
            bright = b;
        }
    }

    const auto seg_ok = [&](const Run& r) {
        return r.bright && r.len >= rg.seg_min && r.len <= rg.seg_max;
    };
    const auto gap_ok = [&](const Run& r) {
        return !r.bright && r.len >= rg.gap_min && r.len <= rg.gap_max;
    };
    for (size_t i = 0; i < runs.size(); ++i) {
        if (!seg_ok(runs[i]))
            continue;
        size_t j = i;
        std::vector<int> starts{runs[i].start}, lens{runs[i].len};
        while (j + 2 < runs.size() && gap_ok(runs[j + 1]) && seg_ok(runs[j + 2])) {
            j += 2;
            starts.push_back(runs[j].start);
            lens.push_back(runs[j].len);
        }
        if (static_cast<int>(starts.size()) >= rg.min_cols) {
            std::vector<int> steps;
            for (size_t k = 1; k < starts.size(); ++k)
                steps.push_back(starts[k] - starts[k - 1]);
            const int period = median_of(steps);
            if (period >= rg.period_min && period <= rg.period_max)
                out.push_back({y, starts.front(), static_cast<int>(starts.size()), period,
                               median_of(lens)});
        }
        i = j; // za lancuchem
    }
}

// Panel = grupa linii o zblizonym x0; wiersze = zlepki kolejnych y.
struct Panel {
    std::vector<PatternLine> lines;
    std::vector<std::pair<int, int>> row_spans; // [min_y, max_y] per wiersz

    int median_x0() const {
        std::vector<int> v;
        for (const auto& l : lines)
            v.push_back(l.x0);
        return median_of(v);
    }
};

// Zbuduj GridSpec z panelu. Wiersze o spojnym dy; slot przyjmowany kwadratowy
// (zmierzone 52x52) — pion sluzy originowi i dy, bok bierzemy z szerokosci
// odcinka ramki na liniach granicznych.
std::optional<GridSpec> grid_from_panel(Panel& p, const Ranges& rg) {
    if (p.lines.size() < 4)
        return std::nullopt;
    std::sort(p.lines.begin(), p.lines.end(),
              [](const PatternLine& a, const PatternLine& b) { return a.y < b.y; });
    // Zlepiaj kolejne y w wiersze (wypelniony wiersz daje linie takze
    // wewnatrz slotow, pusty — tylko na ramkach gora/dol).
    int span_start = p.lines.front().y, prev = span_start;
    for (const auto& l : p.lines) {
        if (l.y - prev > rg.y_glue) {
            p.row_spans.emplace_back(span_start, prev);
            span_start = l.y;
        }
        prev = l.y;
    }
    p.row_spans.emplace_back(span_start, prev);
    // Ramka gorna i dolna wiersza to na ogol OSOBNE pasma odlegle o
    // ~wysokosc slotu (wnetrze nie daje linii wzoru) — sklej je w jeden
    // wiersz. Kolejny wiersz zaczyna sie znacznie dalej (dy - slot) i
    // pozostaje osobnym pasmem.
    std::vector<std::pair<int, int>> merged;
    for (const auto& s : p.row_spans) {
        if (!merged.empty() && s.first - merged.back().second <= rg.seg_max + 4)
            merged.back().second = s.second;
        else
            merged.push_back(s);
    }
    p.row_spans = std::move(merged);
#ifdef PREDEYE_AUTOCAL_DEBUG
    std::fprintf(stderr, "panel x0=%d, %zu linii, wiersze:", p.median_x0(), p.lines.size());
    for (const auto& [a, b] : p.row_spans)
        std::fprintf(stderr, " [%d..%d]", a, b);
    std::fprintf(stderr, "\n");
#endif
    if (p.row_spans.size() < 4)
        return std::nullopt;

    std::vector<int> dys;
    for (size_t i = 1; i < p.row_spans.size(); ++i)
        dys.push_back(p.row_spans[i].first - p.row_spans[i - 1].first);
    std::vector<int> xs, periods, segs, cols;
    for (const auto& l : p.lines) {
        xs.push_back(l.x0);
        periods.push_back(l.period);
        segs.push_back(l.seg);
        cols.push_back(l.cols);
    }
    GridSpec g;
    const int side = median_of(segs);
    g.slot = {side, side};
    g.dx = median_of(periods);
    g.dy = median_of(dys);
    // Linia graniczna zaczyna sie na wewnetrznej krawedzi (rogi ramki sa
    // zaokraglone), origin.y = gorna ramka + 2 px grubosci.
    g.origin = {median_of(xs), p.row_spans.front().first + 2};
    g.cols = median_of(cols);
    g.rows = static_cast<int>(p.row_spans.size());
    return g;
}

} // namespace

std::optional<AutoCalibResult> auto_calibrate(const cv::Mat& frame_bgr) {
    CV_Assert(!frame_bgr.empty() && frame_bgr.type() == CV_8UC3);
    cv::Mat gray;
    cv::cvtColor(frame_bgr, gray, cv::COLOR_BGR2GRAY);
    const Ranges rg = ranges_for(gray.rows);

    std::vector<PatternLine> lines;
    for (int y = 0; y < gray.rows; ++y)
        detect_at(gray, y, rg, lines);
    if (lines.empty())
        return std::nullopt;

    // Grupowanie linii po x0 (mapa posortowana — najblizszy istniejacy klucz).
    std::map<int, Panel> groups;
    for (const auto& l : lines) {
        auto it = groups.lower_bound(l.x0 - rg.x_group);
        if (it == groups.end() || std::abs(it->first - l.x0) > rg.x_group)
            it = groups.emplace(l.x0, Panel{}).first;
        it->second.lines.push_back(l);
    }
    // Dwie najliczniejsze grupy to kandydaci paneli.
    std::vector<Panel*> best;
    for (auto& [x0, p] : groups)
        best.push_back(&p);
    std::sort(best.begin(), best.end(),
              [](const Panel* a, const Panel* b) { return a->lines.size() > b->lines.size(); });
    if (best.size() > 2)
        best.resize(2);

    std::optional<GridSpec> ally, enemy;
    int lines_ally = 0, lines_enemy = 0;
    for (Panel* p : best) {
        auto g = grid_from_panel(*p, rg);
        if (!g)
            continue;
        // Panel sojusznikow lezy w lewej polowie ekranu, wroga w prawej.
        if (g->origin.x < gray.cols / 2 && !ally) {
            ally = g;
            lines_ally = static_cast<int>(p->lines.size());
        } else if (g->origin.x >= gray.cols / 2 && !enemy) {
            enemy = g;
            lines_enemy = static_cast<int>(p->lines.size());
        }
    }
    if (!ally && !enemy)
        return std::nullopt;

    AutoCalibResult res;
    res.calib.resolution = {gray.cols, gray.rows};
    // Brakujacy panel doliczany ze zmierzonego przesuniecia miedzy panelami.
    if (ally && !enemy) {
        GridSpec e = *ally;
        const int panel_offset =
            ally->origin.x - ally_grid_from_enemy(*ally, res.calib.resolution).origin.x;
        e.origin.x = ally->origin.x + panel_offset;
        enemy = e;
        res.note = "panel wroga doliczony z przesuniecia paneli (nie wykryty wprost)";
    } else if (enemy && !ally) {
        ally = ally_grid_from_enemy(*enemy, res.calib.resolution);
        res.note = "panel sojusznikow doliczony z przesuniecia paneli (nie wykryty wprost)";
    }
    res.calib.enemy_item_grid = *enemy;
    res.calib.ally_item_grid = *ally;
    res.lines_ally = lines_ally;
    res.lines_enemy = lines_enemy;
    res.rows_ally = ally->rows;
    res.rows_enemy = enemy->rows;
    return res;
}

} // namespace predeye
