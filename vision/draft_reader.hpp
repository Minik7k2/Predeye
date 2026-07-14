// vision/draft_reader — odczyt ekranu draftu (bany + picki obu druzyn)
// przez dopasowanie portretow bohaterow metoda NCC (ta sama co itemy).
// Modul przenosny (OpenCV, bez WIN32). Wymaga skalibrowanych regionow
// draftu w calibration.json (Calibration::draft) — bez nich zwraca
// wynik z calibrated=false.
#pragma once

#include "core/hero_context.hpp"
#include "vision/calibration.hpp"
#include "vision/icon_matcher.hpp"

#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace predeye {

// Jeden slot draftu (ban albo pick).
struct DraftSlot {
    int index = 0;       // pozycja w siatce (wiersz*cols + kolumna)
    bool empty = true;   // slot jeszcze nie wybrany / poza klatka
    long long hero_id = 0;
    std::string hero_name;
    float cosine = -1.0f;
    bool confident = false;
};

struct DraftRead {
    bool calibrated = false; // false => brak regionow draftu w kalibracji
    std::vector<DraftSlot> ally_bans, enemy_bans;
    std::vector<DraftSlot> ally_picks, enemy_picks;

    // Nazwy pewnie rozpoznanych bohaterow z listy slotow (pomija puste).
    static std::vector<std::string> names(const std::vector<DraftSlot>& slots);
};

// Wytnij ROI wszystkich obecnych siatek draftu i dopasuj portrety.
// ROI poza klatka / puste sloty (jeszcze nie wybrano) => empty.
DraftRead read_draft(const cv::Mat& frame_bgr, const Calibration& calib,
                     const IconMatcher& hero_matcher, const HeroDB& heroes);

} // namespace predeye
