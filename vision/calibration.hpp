// vision/calibration — siatki slotow scoreboardu per rozdzielczosc (§6.8).
#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace predeye {

// Siatka itemow jednej druzyny na scoreboardzie (TAB): rows wierszy (graczy)
// po cols slotow.
struct GridSpec {
    cv::Point origin{0, 0}; // lewy-gorny rog slotu [0][0]
    cv::Size slot{32, 32};  // rozmiar pojedynczego slotu
    int dx = 36;            // krok miedzy slotami w wierszu (origin->origin)
    int dy = 64;            // krok miedzy wierszami
    int cols = 6;
    int rows = 5;

    cv::Rect slot_rect(int row, int col) const {
        return {origin.x + col * dx, origin.y + row * dy, slot.width, slot.height};
    }
};

struct Calibration {
    cv::Size resolution{1920, 1080};
    GridSpec enemy_item_grid; // panel wroga
    GridSpec ally_item_grid;  // panel mojej druzyny

    // Wczytaj calibration.json; wyjatek z czytelnym komunikatem przy bledzie.
    // Starszy plik bez ally_item_grid wczyta sie: siatka sojusznikow dostaje
    // geometrie wroga z originem przesunietym o odstep paneli (orientacyjnie).
    static Calibration load(const std::string& path);
    void save(const std::string& path) const;

    // Punkt startowy dla danej rozdzielczosci. Wartosci 1080p ZMIERZONE na
    // realnych zrzutach (patch 5.4.4); inne rozdzielczosci skalowane
    // wysokoscia — orientacyjne, do potwierdzenia podgladem (preview/GUI).
    static Calibration default_for(const cv::Size& resolution);
};

// Siatka sojusznikow z siatki wroga: uklad wiersza w obu panelach jest
// IDENTYCZNY (nie lustrzany) — panel wroga jest przesuniety w prawo o staly
// odstep, zmierzony na realnych zrzutach 1080p (+1010 px, skala z wysokoscia).
GridSpec ally_grid_from_enemy(const GridSpec& enemy, const cv::Size& resolution);

// Rysuje obie siatki na kopii klatki (wrogowie czerwono, sojusznicy zielono,
// numery wierszy) — podglad do iteracji kalibracji (CLI preview.png i GUI).
cv::Mat draw_grid(const cv::Mat& frame_bgr, const Calibration& calib);

} // namespace predeye
