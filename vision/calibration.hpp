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
    // geometrie wroga z originem odbitym wzgledem srodka ekranu (orientacyjnie).
    static Calibration load(const std::string& path);
    void save(const std::string& path) const;

    // Punkt startowy do iteracji dla danej rozdzielczosci — wartosci
    // ORIENTACYJNE, uzytkownik dostraja je podgladem (preview.png / GUI).
    static Calibration default_for(const cv::Size& resolution);
};

// Orientacyjna siatka sojusznikow z siatki wroga: ta sama geometria, origin.x
// odbity wzgledem srodka ekranu (panele druzyn sa lustrzane na scoreboardzie).
GridSpec mirror_grid(const GridSpec& enemy, const cv::Size& resolution);

// Rysuje obie siatki na kopii klatki (wrogowie czerwono, sojusznicy zielono,
// numery wierszy) — podglad do iteracji kalibracji (CLI preview.png i GUI).
cv::Mat draw_grid(const cv::Mat& frame_bgr, const Calibration& calib);

} // namespace predeye
