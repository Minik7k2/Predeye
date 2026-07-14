// vision/calibration — siatki slotow scoreboardu i draftu per rozdzielczosc (§6.8).
#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace predeye {

// Siatka prostokatow (sloty itemow, portrety bohaterow, sloty banow/pickow):
// rows wierszy po cols slotow.
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
    // Siatka "obecna" — rows=0 oznacza region nieskalibrowany (np. draft
    // przed dostarczeniem zrzutow); czytniki pomijaja takie siatki.
    bool present() const { return rows > 0 && cols > 0; }
};

// Regiony ekranu draftu (bany + picki obu druzyn). Domyslnie nieobecne
// (rows=0) — wymagaja kalibracji na realnym zrzucie ekranu draftu.
struct DraftSpec {
    GridSpec ally_bans, enemy_bans;   // zwykle 1 wiersz x N slotow
    GridSpec ally_picks, enemy_picks; // zwykle kolumna 5 portretow (cols=1)
    bool present() const {
        return ally_bans.present() || enemy_bans.present() || ally_picks.present() ||
               enemy_picks.present();
    }
};

struct Calibration {
    cv::Size resolution{1920, 1080};
    GridSpec enemy_item_grid; // panel itemow wroga
    GridSpec ally_item_grid;  // panel itemow mojej druzyny
    GridSpec enemy_hero_grid; // kolumna portretow wroga (cols=1, wiersz = gracz)
    GridSpec ally_hero_grid;  // kolumna portretow mojej druzyny
    DraftSpec draft;          // ekran draftu (opcjonalny)

    // Wczytaj calibration.json; wyjatek z czytelnym komunikatem przy bledzie.
    // Starsze pliki wczytuja sie: brakujace siatki dostaja wartosci
    // wyprowadzone (lustro / kolumna portretow przy siatce itemow).
    static Calibration load(const std::string& path);
    void save(const std::string& path) const;

    // Punkt startowy do iteracji dla danej rozdzielczosci — wartosci
    // ORIENTACYJNE, uzytkownik dostraja je podgladem (preview.png / GUI).
    static Calibration default_for(const cv::Size& resolution);
};

// Orientacyjna siatka sojusznikow z siatki wroga: ta sama geometria, origin.x
// odbity wzgledem srodka ekranu (panele druzyn sa lustrzane na scoreboardzie).
GridSpec mirror_grid(const GridSpec& enemy, const cv::Size& resolution);

// Orientacyjna kolumna portretow przy siatce itemow: 1 kolumna, ten sam dy
// i liczba wierszy, slot kwadratowy, na lewo (wrog) lub prawo (sojusznik)
// od siatki itemow.
GridSpec hero_grid_for(const GridSpec& item_grid, bool left_of_items);

// Rysuje siatki na kopii klatki (wrogowie czerwono, sojusznicy zielono,
// portrety cienka linia, draft na niebiesko) — podglad do iteracji kalibracji.
cv::Mat draw_grid(const cv::Mat& frame_bgr, const Calibration& calib);

} // namespace predeye
