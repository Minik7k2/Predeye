// vision/vision_session — fasada toru wizyjnego (live): odczyt scoreboardu,
// doostrzenie profilu wroga i swiezy counter-build z diffem miedzy odczytami.
// Wspolna logika CLI (predeye live) i GUI (zakladka Live) — jedno zrodlo
// algorytmu (§6.9/§6.10). Wymaga OpenCV; poza rdzeniem przenosnym.
#pragma once

#include "core/build_engine.hpp"
#include "core/enemy_build.hpp"
#include "core/hero_context.hpp"
#include "core/local_data.hpp"
#include "core/models.hpp"
#include "core/omeda_client.hpp"
#include "core/shopping_advisor.hpp"
#include "vision/auto_calibrate.hpp"
#include "vision/calibration.hpp"
#include "vision/draft_reader.hpp"
#include "vision/icon_matcher.hpp"

#include <map>
#include <memory>
#include <opencv2/core.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace predeye {

// Pojedynczy slot itemu w widoku odczytu (do prezentacji w GUI/CLI).
struct LiveSlotView {
    bool empty = true;
    std::string name;       // display_name; "?" gdy rozpoznane, lecz bez indeksu
    bool confident = false; // cosine >= prog pewnosci
    std::string tooltip_pl; // spolszczenie itemu (co robi / jak dziala); "" gdy brak
};

// Wiersz = jeden gracz (wrog albo sojusznik): rola, sloty i zmiany wzgledem
// poprzedniego odczytu. Rola z pozycji wiersza (scoreboard_row_role) — wiersz
// wroga o tej samej roli to przeciwnik w linii (matchup).
struct LiveRowView {
    int row = 0;
    Role role = Role::Unknown;
    std::string role_label; // "Offlane"... / "Wiersz N" gdy Unknown
    std::string hero_name;  // z portretu; "" gdy nierozpoznany/brak matchera
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

// Wynik jednego odczytu: obie druzyny, profil wroga, counter-build
// i kolejka zakupow ("co kupic w nastepnej kolejce i dlaczego").
struct LiveResult {
    std::vector<LiveRowView> enemies;
    std::vector<LiveRowView> allies;
    int total_items = 0;
    int uncertain = 0;
    EnemyProfile profile;
    BuildResult counter;
    std::string objective_name;
    // Zakupy: counter minus itemy odczytane z MOJEGO wiersza sojusznikow
    // (wiersz o roli celu); uzasadnienia po polsku (pl_items + tagi counter).
    ShoppingAdvice shopping;
    std::string shopping_note; // skad wzieto "moje" itemy (albo czemu ich brak)
};

// Trzyma stan toru live: dane API, baze ikon, cel gracza i poprzedni odczyt
// (do diffa). Metody sieciowe (ensure_*) rzucaja wyjatkiem — warstwa UI lapie.
class VisionSession {
  public:
    explicit VisionSession(std::string cache_dir = OmedaClient::default_dir());

    void ensure_loaded();  // items + heroes + silnik (sieciowe, cache TTL)
    void ensure_matcher(); // baza ikon (pobiera brakujace); wymaga ensure_loaded
    // Baza portretow bohaterow (rozpoznawanie na scoreboardzie i w draftcie).
    void ensure_hero_matcher();

    bool matcher_ready() const { return matcher_ && matcher_->base_size() > 0; }
    bool hero_matcher_ready() const { return hero_matcher_ && hero_matcher_->base_size() > 0; }
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
    // Gdy gotowy jest matcher bohaterow: tozsamosc per wiersz + tie-break
    // niepewnych itemow po typowym buildzie rozpoznanego bohatera.
    LiveResult read(const cv::Mat& frame, const Calibration& calib);

    // Odczyt ekranu draftu (bany/picki). Wymaga skalibrowanych regionow
    // draftu; buduje matcher bohaterow przy pierwszym uzyciu (sieciowe).
    DraftRead read_draft_frame(const cv::Mat& frame, const Calibration& calib);

    // Auto-kalibracja siatek itemow na klatce (start: `base` albo
    // default_for). Buduje matcher ikon przy pierwszym uzyciu (sieciowe).
    AutoCalibResult auto_calibrate_frame(const cv::Mat& frame);
    AutoCalibResult auto_calibrate_frame(const cv::Mat& frame, const Calibration& base);

    const HeroDB* hero_db() {
        ensure_loaded();
        return heroes_.get();
    }

  private:
    // Zbior id itemow typowego buildu bohatera (cache per hero_id na sesje);
    // nullopt w mapie = "sprawdzono, brak buildow" (nie pytaj API ponownie).
    const std::set<long long>* typical_items(long long hero_id, const std::string& name);

    std::string icon_dir_, hero_icon_dir_;
    OmedaClient api_;
    LocalData local_; // spolszczenia itemow + dane wlasciciela (data/)
    std::vector<Item> items_;
    ItemIndex index_;
    std::unique_ptr<HeroDB> heroes_;
    std::unique_ptr<BuildEngine> engine_;
    std::unique_ptr<IconMatcher> matcher_;
    std::unique_ptr<IconMatcher> hero_matcher_;
    Objective obj_;
    Role role_ = Role::Unknown; // rola celu — wskazuje moj wiersz sojusznikow
    bool has_obj_ = false;
    bool loaded_ = false;
    std::vector<std::set<std::string>> prev_enemy_; // nazwy itemow per wiersz
    std::vector<std::set<std::string>> prev_ally_;
    std::map<long long, std::optional<std::set<long long>>> typical_cache_;
};

} // namespace predeye
