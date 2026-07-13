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

// Przygotowanie grafiki API (webp z alfa, biala winieta, marginesy) do
// postaci kafelka jak na scoreboardzie: kwadratowy bbox po alfie +
// kompozycja na ciemnym tle kafelka. Obrazy bez alfy wracaja bez zmian.
// Wspolna sciezka bazy sygnatur i narzedzi (icon_harness) — sondy i baza
// musza byc przygotowane identycznie.
cv::Mat tile_from_asset(const cv::Mat& img);

// Grafika do pobrania do lokalnego katalogu (webp per id + manifest).
struct ImageRef {
    long long id = 0;
    std::string image; // sciezka API "/assets/....webp"
    std::string label; // do komunikatow bledow (display_name)
};

// Manifest katalogu grafik (id -> plik); pusty gdy katalogu/manifestu brak.
std::map<long long, std::string> load_image_manifest(const std::string& dir);

// Zapewnia lokalny katalog grafik z API: pobiera brakujace (pauza ~50 ms),
// utrzymuje manifest. Wspolne dla ikon itemow i portretow bohaterow.
std::map<long long, std::string> ensure_image_cache(const std::vector<ImageRef>& wanted,
                                                    OmedaClient& api, const std::string& dir);

// Dwustopniowe dopasowanie NCC probki do bazy sygnatur (wiersz = sygnatura
// jednego id): czyste NCC wylania kandydatow, potem najlepsze wyrownanie
// probki po przesunieciach +/-2 px. Wspolne dla itemow i bohaterow.
MatchResult match_signatures(const cv::Mat& signatures, const std::vector<long long>& ids,
                             const cv::Mat& probe_bgr);

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
