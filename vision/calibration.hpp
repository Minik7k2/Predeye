// vision/calibration — siatka slotow scoreboardu per rozdzielczosc (§6.8).
#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace predeye {

// Siatka itemow wroga na scoreboardzie (TAB): rows wierszy (wrogow)
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
    GridSpec enemy_item_grid;

    // Wczytaj calibration.json; wyjatek z czytelnym komunikatem przy bledzie.
    static Calibration load(const std::string& path);
    void save(const std::string& path) const;

    // Punkt startowy do iteracji dla danej rozdzielczosci — wartosci
    // ORIENTACYJNE, uzytkownik dostraja je podgladem (preview.png).
    static Calibration default_for(const cv::Size& resolution);
};

// Rysuje siatke na kopii klatki (ramki slotow + numery wierszy) — podglad
// do recznej iteracji wartosci w JSON. Bez GUI.
cv::Mat draw_grid(const cv::Mat& frame_bgr, const Calibration& calib);

} // namespace predeye
