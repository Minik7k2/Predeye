// vision/vision_session — fasada toru wizyjnego (live): odczyt scoreboardu,
// doostrzenie profilu wroga i swiezy counter-build z diffem miedzy odczytami.
// Wspolna logika CLI (predeye live) i GUI (zakladka Live) — jedno zrodlo
// algorytmu (§6.9/§6.10). Wymaga OpenCV; poza rdzeniem przenosnym.
#pragma once

#include "core/build_engine.hpp"
#include "core/enemy_build.hpp"
#include "core/hero_context.hpp"
#include "core/models.hpp"
#include "core/omeda_client.hpp"
#include "vision/calibration.hpp"
#include "vision/icon_matcher.hpp"

#include <memory>
#include <opencv2/core.hpp>
#include <set>
#include <string>
#include <vector>

namespace predeye {

// Pojedynczy slot itemu w widoku odczytu (do prezentacji w GUI/CLI).
struct LiveSlotView {
    bool empty = true;
    std::string name;       // display_name; "?" gdy rozpoznane, lecz bez indeksu
    bool confident = false; // cosine >= prog pewnosci
};

// Wiersz = jeden wrog: sloty + zmiany wzgledem poprzedniego odczytu.
struct LiveEnemyView {
    int row = 0;
    std::vector<LiveSlotView> slots;
    std::vector<std::string> changes; // "+Item" / "-Item" wzgledem poprzednika

    bool has_any() const {
        for (const auto& s : slots)
            if (!s.empty)
                return true;
        return false;
    }
};

// Wynik jednego odczytu: rozpoznane itemy, profil wroga i counter-build.
struct LiveResult {
    std::vector<LiveEnemyView> enemies;
    int total_items = 0;
    int uncertain = 0;
    EnemyProfile profile;
    BuildResult counter;
    std::string objective_name;
};

// Trzyma stan toru live: dane API, baze ikon, cel gracza i poprzedni odczyt
// (do diffa). Metody sieciowe (ensure_*) rzucaja wyjatkiem — warstwa UI lapie.
class VisionSession {
  public:
    explicit VisionSession(std::string cache_dir = OmedaClient::default_dir());

    void ensure_loaded();  // items + heroes + silnik (sieciowe, cache TTL)
    void ensure_matcher(); // baza ikon (pobiera brakujace); wymaga ensure_loaded

    bool matcher_ready() const { return matcher_ && matcher_->base_size() > 0; }
    size_t icon_base_size() const { return matcher_ ? matcher_->base_size() : 0; }

    // Lista nazw bohaterow (display_name, posortowana) — do listy wyboru.
    std::vector<std::string> hero_names();

    // Ustaw cel gracza (bohater + rola); rzuca gdy bohater nieznany.
    // Zwraca rozpoznana nazwe display_name. Wymaga ensure_loaded.
    std::string set_objective(const std::string& hero, Role role, int budget_override = -1);
    bool has_objective() const { return has_obj_; }

    void reset_diff() { prev_.clear(); }

    // Jeden odczyt klatki -> itemy per wrog + profil + counter + diff.
    // Aktualizuje wewnetrzny stan do kolejnego diffa.
    LiveResult read(const cv::Mat& frame, const Calibration& calib);

  private:
    std::string icon_dir_;
    OmedaClient api_;
    std::vector<Item> items_;
    ItemIndex index_;
    std::unique_ptr<HeroDB> heroes_;
    std::unique_ptr<BuildEngine> engine_;
    std::unique_ptr<IconMatcher> matcher_;
    Objective obj_;
    bool has_obj_ = false;
    bool loaded_ = false;
    std::vector<std::set<std::string>> prev_; // nazwy itemow per wiersz
};

} // namespace predeye
