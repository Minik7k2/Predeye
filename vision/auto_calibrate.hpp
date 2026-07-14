// vision/auto_calibrate — automatyczne doprecyzowanie siatki scoreboardu:
// start z Calibration::default_for(rozdzielczosc), potem przeszukanie NCC
// przesuniec originu (kotwica = "jak bardzo ROI wyglada jak ikona z bazy").
// Uzywane przy pierwszym odczycie live bez pliku kalibracji: pewny wynik
// zapisuje sie sam, niepewny wysyla uzytkownika do recznej kalibracji.
// Ograniczenie: wymaga NIEPUSTYCH slotow (ktos ma itemy) — na starcie meczu
// bez itemow pewnosc bedzie niska; wtedy odczyt i tak jest pusty.
#pragma once

#include "vision/calibration.hpp"
#include "vision/icon_matcher.hpp"

#include <opencv2/core.hpp>

namespace predeye {

struct AutoCalibResult {
    Calibration calib;       // default_for + znalezione przesuniecia siatek
    float enemy_confidence = -1.0f; // sredni najlepszy cosine slotow kotwicy
    float ally_confidence = -1.0f;
    // Prog wstepny — do weryfikacji na realnych zrzutach (M4).
    static constexpr float kThreshold = 0.60f;
    bool ok() const { return enemy_confidence >= kThreshold; }
};

// Doprecyzuj siatki itemow obu druzyn na klatce; `base` = punkt startowy
// (np. default_for albo wczesniejsza kalibracja do odswiezenia).
AutoCalibResult auto_calibrate(const cv::Mat& frame_bgr, const IconMatcher& matcher,
                               const Calibration& base);

// Wariant od zera: base = Calibration::default_for(rozmiar klatki).
AutoCalibResult auto_calibrate(const cv::Mat& frame_bgr, const IconMatcher& matcher);

} // namespace predeye
