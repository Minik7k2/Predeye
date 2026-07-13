// vision/auto_calibration — automatyczna detekcja siatek scoreboardu (M4).
// Metoda: sloty maja jasne ~2 px ramki na ciemnym panelu; na liniach ramek
// (i przez wypelnione wiersze) widac okresowy wzor "jasny odcinek ~slot,
// ciemna przerwa" o kroku dx. Zwalidowane recznie skanami RLE na realnych
// zrzutach 1080p (dwa mecze zgodne co do piksela).
#pragma once

#include "calibration.hpp"

#include <opencv2/core.hpp>
#include <optional>
#include <string>

namespace predeye {

struct AutoCalibResult {
    Calibration calib;
    int lines_ally = 0;  // linie wzoru potwierdzajace panel (im wiecej, tym pewniej)
    int lines_enemy = 0;
    int rows_ally = 0;   // wykryte wiersze (oczekiwane 5)
    int rows_enemy = 0;
    std::string note;    // diagnostyka po polsku (np. panel doliczony z przesuniecia)
};

// Wykryj obie siatki na klatce TAB. nullopt, gdy zaden panel nie dal spojnej
// siatki — wtedy zostaje sciezka reczna (default_for + podglad).
std::optional<AutoCalibResult> auto_calibrate(const cv::Mat& frame_bgr);

} // namespace predeye
