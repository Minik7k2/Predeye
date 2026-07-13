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
#include "vision/hero_matcher.hpp"
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

// Wiersz = jeden gracz (wrog albo sojusznik): rola, sloty i zmiany wzgledem
// poprzedniego odczytu. Rola z pozycji wiersza (scoreboard_row_role) — wiersz
// wroga o tej samej roli to przeciwnik w linii (matchup).
struct LiveRowView {
    int row = 0;
    Role role = Role::Unknown;
    std::string role_label; // "Offlane"... / "Wiersz N" gdy Unknown
    std::string hero;       // display_name z portretu ("" gdy nie rozpoznano)
    bool hero_confident = false;
    std::vector<LiveSlotView> slots;
    std::vector<std::string> changes; // "+Item" / "-Item" wzgledem poprzednika

    bool has_any() const {
        for (const auto& s : slots)
            if (!s.empty)
                return true;
        return false;
    }
};

// Wynik jednego odczytu: obie druzyny, profil wroga i counter-build.
struct LiveResult {
    std::vector<LiveRowView> enemies;
    std::vector<LiveRowView> allies;
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
    void ensure_matcher(); // bazy ikon i portretow (pobiera brakujace); wymaga ensure_loaded

    bool matcher_ready() const { return matcher_ && matcher_->base_size() > 0; }
    size_t icon_base_size() const { return matcher_ ? matcher_->base_size() : 0; }

    // Lista nazw bohaterow (display_name, posortowana) — do listy wyboru.
    std::vector<std::string> hero_names();

    // Ustaw cel gracza (bohater + rola); rzuca gdy bohater nieznany.
    // Zwraca rozpoznana nazwe display_name. Wymaga ensure_loaded.
    std::string set_objective(const std::string& hero, Role role, int budget_override = -1);
    bool has_objective() const { return has_obj_; }

    void reset_diff() {
        prev_enemy_.clear();
        prev_ally_.clear();
    }

    // Jeden odczyt klatki -> itemy per gracz (obie druzyny) + profil wroga
    // + counter + diff. Aktualizuje wewnetrzny stan do kolejnego diffa.
    LiveResult read(const cv::Mat& frame, const Calibration& calib);

  private:
    std::string icon_dir_;
    std::string portrait_dir_;
    OmedaClient api_;
    std::vector<Item> items_;
    ItemIndex index_;
    std::unique_ptr<HeroDB> heroes_;
    std::unique_ptr<BuildEngine> engine_;
    std::unique_ptr<IconMatcher> matcher_;
    std::unique_ptr<HeroMatcher> hero_matcher_;
    Objective obj_;
    bool has_obj_ = false;
    bool loaded_ = false;
    std::vector<std::set<std::string>> prev_enemy_; // nazwy itemow per wiersz
    std::vector<std::set<std::string>> prev_ally_;
};

} // namespace predeye
