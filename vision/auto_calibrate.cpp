#include "auto_calibrate.hpp"

#include <algorithm>
#include <vector>

namespace predeye {
namespace {

// Ocena przesuniecia (ox, oy) siatki: sredni najlepszy cosine NCC po
// niepustych slotach kotwicy. Kotwica = kolumna 0 wszystkich wierszy
// (pierwszy item gracza jest kupowany najwczesniej) + kolumna 1 — do 6 ROI.
// Zwraca -1, gdy zaden slot kotwicy nie jest uzyteczny (pusto / poza klatka).
float score_offset(const cv::Mat& frame, const GridSpec& g, int ox, int oy,
                   const IconMatcher& matcher) {
    const cv::Rect frame_rect(0, 0, frame.cols, frame.rows);
    float sum = 0.0f;
    int used = 0;
    for (int c = 0; c < std::min(2, g.cols) && used < 6; ++c) {
        for (int r = 0; r < g.rows && used < 6; ++r) {
            cv::Rect rect = g.slot_rect(r, c);
            rect.x += ox;
            rect.y += oy;
            if ((rect & frame_rect) != rect || rect.area() == 0)
                continue;
            const cv::Mat roi = frame(rect);
            if (IconMatcher::looks_empty(roi))
                continue;
            sum += matcher.best_cosine(roi);
            ++used;
        }
    }
    return used == 0 ? -1.0f : sum / static_cast<float>(used);
}

// Najlepsze przesuniecie siatki: przebieg zgrubny (siatka co `step` w oknie
// +/-radius), potem doprecyzowanie co 1 px wokol najlepszego punktu.
struct Offset {
    int x = 0, y = 0;
    float score = -1.0f;
};

Offset best_offset(const cv::Mat& frame, const GridSpec& g, const IconMatcher& matcher) {
    Offset best;
    constexpr int kRadius = 48, kStep = 6;
    for (int oy = -kRadius; oy <= kRadius; oy += kStep) {
        for (int ox = -kRadius; ox <= kRadius; ox += kStep) {
            const float s = score_offset(frame, g, ox, oy, matcher);
            if (s > best.score)
                best = {ox, oy, s};
        }
    }
    if (best.score < 0.0f)
        return best; // brak uzytecznych slotow — nie ma czego doprecyzowywac
    Offset fine = best;
    for (int oy = best.y - kStep; oy <= best.y + kStep; ++oy) {
        for (int ox = best.x - kStep; ox <= best.x + kStep; ++ox) {
            if (ox == best.x && oy == best.y)
                continue;
            const float s = score_offset(frame, g, ox, oy, matcher);
            if (s > fine.score)
                fine = {ox, oy, s};
        }
    }
    return fine;
}

} // namespace

AutoCalibResult auto_calibrate(const cv::Mat& frame_bgr, const IconMatcher& matcher,
                               const Calibration& base) {
    AutoCalibResult out;
    out.calib = base;
    out.calib.resolution = frame_bgr.size();

    const Offset e = best_offset(frame_bgr, base.enemy_item_grid, matcher);
    out.enemy_confidence = e.score;
    if (e.score >= 0.0f) {
        out.calib.enemy_item_grid.origin.x += e.x;
        out.calib.enemy_item_grid.origin.y += e.y;
        // Portrety jada razem z siatka itemow (staly uklad wiersza).
        out.calib.enemy_hero_grid.origin.x += e.x;
        out.calib.enemy_hero_grid.origin.y += e.y;
    }

    // Siatka sojusznikow doprecyzowana niezaleznie (lustro bywa niedokladne).
    const Offset a = best_offset(frame_bgr, base.ally_item_grid, matcher);
    out.ally_confidence = a.score;
    if (a.score >= AutoCalibResult::kThreshold) {
        out.calib.ally_item_grid.origin.x += a.x;
        out.calib.ally_item_grid.origin.y += a.y;
        out.calib.ally_hero_grid.origin.x += a.x;
        out.calib.ally_hero_grid.origin.y += a.y;
    }
    return out;
}

AutoCalibResult auto_calibrate(const cv::Mat& frame_bgr, const IconMatcher& matcher) {
    return auto_calibrate(frame_bgr, matcher, Calibration::default_for(frame_bgr.size()));
}

} // namespace predeye
