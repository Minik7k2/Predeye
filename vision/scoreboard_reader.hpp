// vision/scoreboard_reader — odczyt itemow obu druzyn ze scoreboardu (§6.9).
// Klatka + kalibracja -> ROI slotow -> looks_empty? -> IconMatcher -> itemy
// per wiersz (gracz), osobno dla siatki wroga i sojusznikow.
// Modul przenosny (OpenCV, bez WIN32) — capture/hotkey sa osobno.
#pragma once

#include "core/hero_context.hpp"
#include "core/models.hpp"
#include "vision/calibration.hpp"
#include "vision/icon_matcher.hpp"

#include <array>
#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>

namespace predeye {

// Pojedynczy slot itemu w siatce druzyny.
struct SlotRead {
    int row = 0, col = 0;
    bool empty = true;      // looks_empty albo ROI poza klatka -> slot pominiety
    long long item_id = 0;  // 0 gdy pusty/nierozpoznany
    std::string name;       // display_name rozpoznanego itemu (do wydruku)
    float cosine = -1.0f;   // pewnosc dopasowania NCC
    bool confident = false; // cosine >= MatchResult::kConfidenceThreshold
    std::array<std::pair<long long, float>, 3> top3{}; // kandydaci malejaco
};

// Odczyt jednego wiersza = jednego gracza (wroga albo sojusznika).
struct RowRead {
    int row = 0;
    std::vector<Item> items;     // rozpoznane, niepuste sloty (do klasyfikacji)
    std::vector<SlotRead> slots; // wszystkie sloty wiersza (z pustymi) — do wydruku
    int uncertain = 0;           // ile rozpoznan ponizej progu pewnosci

    // Tozsamosc bohatera z portretu (0/"" gdy brak siatki portretow albo
    // matchera bohaterow) — odblokowuje tie-break itemow po typical_build.
    long long hero_id = 0;
    std::string hero_name;
    float hero_cosine = -1.0f;
    bool hero_confident = false;
};

// Wynik odczytu calego scoreboardu: obie druzyny.
struct ScoreboardRead {
    std::vector<RowRead> enemies; // wiersze siatki wroga
    std::vector<RowRead> allies;  // wiersze siatki mojej druzyny
    int total_items = 0;          // suma rozpoznanych niepustych slotow (obie druzyny)
    int uncertain = 0;            // suma niepewnych rozpoznan (obie druzyny)
};

// Rola gracza w danym wierszu scoreboardu. ZALOZENIE (do weryfikacji na
// realnych zrzutach, M4): Predecessor sortuje wiersze obu druzyn wg rol
// w stalym porzadku Offlane, Jungle, Midlane, Carry, Support. Wiersz i-ty
// wroga gra przeciw wierszowi i-temu sojusznikow (ta sama rola = matchup).
// Inna liczba wierszy niz 5 => Unknown (nie zgadujemy).
Role scoreboard_row_role(int row, int rows);

// Wytnij rows x cols ROI z obu siatek, odfiltruj puste, dopasuj ikony.
// ROI wychodzace poza klatke (zla kalibracja) sa traktowane jak puste —
// funkcja nigdy nie rzuca z powodu zlej geometrii.
// `hero_matcher`/`heroes` (opcjonalne): dopasowanie portretow z siatek
// *_hero_grid daje tozsamosc bohatera per wiersz.
ScoreboardRead read_scoreboard(const cv::Mat& frame_bgr, const Calibration& calib,
                               const IconMatcher& matcher, const ItemIndex& index,
                               const IconMatcher* hero_matcher = nullptr,
                               const HeroDB* heroes = nullptr);

} // namespace predeye
