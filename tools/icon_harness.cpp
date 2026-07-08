// icon_harness — walidacja metody kolor-NCC 32x32 na pelnej bazie ikon (§9).
//
// Symuluje znieksztalcenia toru capture: przeskalowanie do 32-54 px,
// kontrast x0.85-1.2, jasnosc +/-25, szum gaussowski sigma 4-12, jitter +/-2 px.
// Dwa rezimy: realistyczny (lagodna polowa zakresow) i pesymistyczny
// (koncowki zakresow). Progi WIAZACE: top-1 >= 99% (realist.), >= 95%
// (pesym.), top-3 = 100%.
//
// Uzycie: icon_harness <katalog_ikon> [prob_na_ikone=5]
// Wymaga wczesniejszego `predeye fetch-icons` (baza + manifest w katalogu).

#include "vision/icon_matcher.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <random>

namespace fs = std::filesystem;
using namespace predeye;

namespace {

struct Regime {
    const char* name;
    double size_min, size_max;      // px po przeskalowaniu
    double contrast_min, contrast_max;
    double bright_min, bright_max;
    double sigma_min, sigma_max;    // szum gaussowski
    int jitter_max;                 // przesuniecie w px
};

// Pelne zakresy WIAZACE z §9; realistyczny = lagodniejsza polowa,
// pesymistyczny = gorsze koncowki (male ikony, mocny szum).
constexpr Regime kRealistic = {"realistyczny", 40, 54, 0.95, 1.10, -10, 10, 4, 8, 1};
constexpr Regime kPessimistic = {"pesymistyczny", 32, 44, 0.85, 1.20, -25, 25, 8, 12, 2};

cv::Mat distort(const cv::Mat& icon, const Regime& r, std::mt19937& rng) {
    std::uniform_real_distribution<double> size_d(r.size_min, r.size_max);
    std::uniform_real_distribution<double> con_d(r.contrast_min, r.contrast_max);
    std::uniform_real_distribution<double> bri_d(r.bright_min, r.bright_max);
    std::uniform_real_distribution<double> sig_d(r.sigma_min, r.sigma_max);
    std::uniform_int_distribution<int> jit_d(-r.jitter_max, r.jitter_max);

    // 1) skala jak w realnym slocie scoreboardu
    const int side = static_cast<int>(size_d(rng));
    cv::Mat m;
    cv::resize(icon, m, cv::Size(side, side), 0, 0, cv::INTER_AREA);

    // 2) kontrast + jasnosc (rozne ustawienia gry/monitora)
    m.convertTo(m, CV_8UC3, con_d(rng), bri_d(rng));

    // 3) szum gaussowski (kompresja/capture)
    cv::Mat noise(m.size(), CV_32FC3);
    cv::randn(noise, 0.0, sig_d(rng));
    cv::Mat f;
    m.convertTo(f, CV_32FC3);
    f += noise;
    f.convertTo(m, CV_8UC3);

    // 4) jitter — niedokladnosc kalibracji ROI (przesuniecie z replikacja brzegu)
    const int dx = jit_d(rng), dy = jit_d(rng);
    const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    cv::warpAffine(m, m, shift, m.size(), cv::INTER_NEAREST, cv::BORDER_REPLICATE);
    return m;
}

struct Score {
    long long trials = 0, top1 = 0, top3 = 0;
    double pct1() const { return trials ? 100.0 * static_cast<double>(top1) / static_cast<double>(trials) : 0.0; }
    double pct3() const { return trials ? 100.0 * static_cast<double>(top3) / static_cast<double>(trials) : 0.0; }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uzycie: icon_harness <katalog_ikon> [prob_na_ikone=5]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int trials = argc > 2 ? std::atoi(argv[2]) : 5;

    std::ifstream min(fs::path(dir) / "manifest.json", std::ios::binary);
    if (!min) {
        std::fprintf(stderr, "brak %s/manifest.json — uruchom najpierw: predeye fetch-icons\n",
                     dir.c_str());
        return 2;
    }
    const auto manifest = nlohmann::json::parse(min);

    // Zrodlowe ikony do znieksztalcania — pelne rozdzielczosci z cache.
    std::vector<std::pair<long long, cv::Mat>> icons;
    for (const auto& [k, v] : manifest.items()) {
        cv::Mat img = cv::imread((fs::path(dir) / v.get<std::string>()).string(),
                                 cv::IMREAD_COLOR);
        if (!img.empty())
            icons.emplace_back(std::stoll(k), img);
    }
    std::printf("baza: %d ikon, %d prob/ikone/rezim\n", static_cast<int>(icons.size()), trials);
    if (icons.empty())
        return 2;

    // Walidujemy REALNA sciezke dopasowania (IconMatcher::match, offline).
    const IconMatcher matcher(dir);

    std::mt19937 rng(42); // deterministycznie — wyniki powtarzalne
    bool ok = true;
    for (int ri = 0; ri < 2; ++ri) {
        const Regime& regime = ri ? kPessimistic : kRealistic;
        const double need1 = ri ? 95.0 : 99.0;
        Score sc;
        for (const auto& [id, icon] : icons) {
            for (int t = 0; t < trials; ++t) {
                const cv::Mat probe = distort(icon, regime, rng);
                const MatchResult res = matcher.match(probe);
                ++sc.trials;
                if (res.item_id == id)
                    ++sc.top1;
                for (const auto& [tid, cosine] : res.top3) {
                    (void)cosine;
                    if (tid == id) {
                        ++sc.top3;
                        break;
                    }
                }
            }
        }
        const bool pass = sc.pct1() >= need1 && sc.pct3() >= 100.0;
        std::printf("rezim %-13s top-1 %6.2f%% (prog %.0f%%)  top-3 %6.2f%% (prog 100%%)  %s\n",
                    regime.name, sc.pct1(), need1, sc.pct3(), pass ? "OK" : "PONIZEJ PROGU");
        ok = ok && pass;
    }
    return ok ? 0 : 1;
}
