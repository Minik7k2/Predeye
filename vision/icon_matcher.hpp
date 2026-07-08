// vision/icon_matcher — rozpoznawanie itemow metoda kolor-NCC 32x32 (WIAZACE §6.6).
#pragma once

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

// Zapewnia lokalna baze ikon kupowalnych itemow: pobiera brakujace
// (pauza ~50 ms), utrzymuje manifest id -> plik. Zwraca manifest.
std::map<long long, std::string> ensure_icon_cache(const std::vector<Item>& items,
                                                   OmedaClient& api,
                                                   const std::string& icon_cache_dir);

class IconMatcher {
  public:
    // Buduje baze sygnatur z ikon (pobiera brakujace przez api).
    explicit IconMatcher(const std::vector<Item>& items, OmedaClient& api,
                         const std::string& icon_cache_dir);
    // Wariant offline: baza wprost z manifestu w katalogu (bez sieci) —
    // dla narzedzi (icon_harness) i testow.
    explicit IconMatcher(const std::string& icon_cache_dir);

    // Dopasowanie NCC z tolerancja na niedokladnosc ROI: probka jest
    // dodatkowo probowana w przesunieciach +/-2 px (per item liczy sie
    // najlepszy cosine). Kalibracja siatki nigdy nie jest pixel-perfect.
    MatchResult match(const cv::Mat& slot_bgr) const;
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
