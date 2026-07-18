// vision/icon_matcher — rozpoznawanie ikon metoda kolor-NCC 32x32 (WIAZACE §6.6).
// Silnik jest ogolny: baza sygnatur to dowolny zbior (id -> plik obrazka) —
// te sama klasa rozpoznaje itemy (ikony z API), bohaterow (portrety z API)
// i eternalsy (ikony z data/eternals/icons).
#pragma once

#include "core/hero_context.hpp"
#include "core/models.hpp"
#include "core/omeda_client.hpp"

#include <array>
#include <map>
#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>

namespace predeye {

struct MatchResult {
    long long item_id = 0;
    float cosine = -1.0f;
    std::array<std::pair<long long, float>, 3> top3{}; // malejaco po cosine
    bool confident() const { return cosine >= kConfidenceThreshold; }
    // Prog zweryfikowac empirycznie w M4 na realnych zrzutach.
    static constexpr float kConfidenceThreshold = 0.55f;
};

// Sygnatura NCC: 32x32 BGR -> CV_32F 1x3072 -> minus srednia -> / norma L2.
cv::Mat ncc_signature(const cv::Mat& bgr);

// Zapewnia lokalna baze obrazkow z API: pobiera brakujace (pauza ~50 ms
// w get_binary), utrzymuje manifest id -> plik. Zwraca manifest.
// `sources` = pary (id, sciezka "image" z API); `label` tylko do logow.
std::map<long long, std::string>
ensure_image_cache(const std::vector<std::pair<long long, std::string>>& sources,
                   OmedaClient& api, const std::string& cache_dir, const char* label);

// Baza ikon kupowalnych itemow (wrapper na ensure_image_cache).
std::map<long long, std::string> ensure_icon_cache(const std::vector<Item>& items,
                                                   OmedaClient& api,
                                                   const std::string& icon_cache_dir);

// Baza portretow bohaterow (do rozpoznawania na scoreboardzie i w draftcie).
std::map<long long, std::string> ensure_hero_portrait_cache(const std::vector<HeroProfile>& heroes,
                                                            OmedaClient& api,
                                                            const std::string& cache_dir);

class IconMatcher {
  public:
    // Buduje baze sygnatur z ikon (pobiera brakujace przez api).
    explicit IconMatcher(const std::vector<Item>& items, OmedaClient& api,
                         const std::string& icon_cache_dir);
    // Wariant offline: baza wprost z manifestu w katalogu (bez sieci) —
    // dla narzedzi (icon_harness) i testow.
    explicit IconMatcher(const std::string& icon_cache_dir);
    // Baza z jawnego manifestu (id -> plik wzgledem dir) — portrety bohaterow,
    // eternalsy i inne zbiory spoza manifest.json.
    IconMatcher(const std::map<long long, std::string>& manifest, const std::string& dir);

    // Dopasowanie NCC z tolerancja na niedokladnosc ROI: probka jest
    // dodatkowo probowana w przesunieciach +/-2 px (per item liczy sie
    // najlepszy cosine). Kalibracja siatki nigdy nie jest pixel-perfect.
    MatchResult match(const cv::Mat& slot_bgr) const;
    // Najlepszy cosine czystego NCC (bez przesuniec) — tania funkcja oceny
    // "czy to wyglada jak cos z bazy" do auto-kalibracji.
    float best_cosine(const cv::Mat& slot_bgr) const;
    size_t base_size() const { return ids_.size(); }
    const std::vector<long long>& ids() const { return ids_; }

    // Heurystyka pustego slotu (ciemny/jednolity ROI). Progi wstepne —
    // do strojenia na realnych zrzutach w M4.
    static bool looks_empty(const cv::Mat& slot_bgr);

  private:
    void build_base(const std::map<long long, std::string>& manifest,
                    const std::string& icon_cache_dir);

    cv::Mat signatures_; // N x 3072, CV_32F, wiersz = sygnatura itemu
    std::vector<long long> ids_;
};

} // namespace predeye
