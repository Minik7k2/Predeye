// vision/scoreboard_reader — odczyt itemow wrogow ze scoreboardu (§6.9).
// Klatka + kalibracja -> ROI slotow -> looks_empty? -> IconMatcher -> itemy per wrog.
// Modul przenosny (OpenCV, bez WIN32) — capture/hotkey sa osobno.
#pragma once

#include "core/models.hpp"
#include "vision/calibration.hpp"
#include "vision/icon_matcher.hpp"

#include <array>
#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>

namespace predeye {

// Pojedynczy slot itemu w siatce wroga.
struct SlotRead {
    int row = 0, col = 0;
    bool empty = true;       // looks_empty albo ROI poza klatka -> slot pominiety
    long long item_id = 0;   // 0 gdy pusty/nierozpoznany
    std::string name;        // display_name rozpoznanego itemu (do wydruku)
    float cosine = -1.0f;    // pewnosc dopasowania NCC
    bool confident = false;  // cosine >= MatchResult::kConfidenceThreshold
    std::array<std::pair<long long, float>, 3> top3{}; // kandydaci malejaco
};

// Odczyt jednego wiersza = jednego wroga.
struct EnemyRead {
    int row = 0;
    std::vector<Item> items;     // rozpoznane, niepuste sloty (do klasyfikacji)
    std::vector<SlotRead> slots; // wszystkie sloty wiersza (z pustymi) — do wydruku
    int uncertain = 0;           // ile rozpoznan ponizej progu pewnosci
};

// Wynik odczytu calego panelu wroga.
struct ScoreboardRead {
    std::vector<EnemyRead> enemies; // rows wierszy
    int total_items = 0;            // suma rozpoznanych niepustych slotow
    int uncertain = 0;              // suma niepewnych rozpoznan
};

// Wytnij rows x cols ROI, odfiltruj puste, dopasuj ikony -> itemy per wrog.
// ROI wychodzace poza klatke (zla kalibracja) sa traktowane jak puste —
// funkcja nigdy nie rzuca z powodu zlej geometrii.
ScoreboardRead read_scoreboard(const cv::Mat& frame_bgr, const Calibration& calib,
                               const IconMatcher& matcher, const ItemIndex& index);

} // namespace predeye
