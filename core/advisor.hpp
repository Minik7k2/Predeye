// core/advisor — fasada rdzenia: build i counter jako gotowe struktury wynikowe.
// Enkapsuluje OmedaClient + HeroDB + BuildEngine, tak by CLI i GUI dzielily
// jedna logike bez drukowania. Modul przenosny (bez OpenCV).
#pragma once

#include "build_engine.hpp"
#include "draft_advisor.hpp"
#include "enemy_build.hpp"
#include "hero_context.hpp"
#include "loadout_advisor.hpp"
#include "local_data.hpp"
#include "models.hpp"
#include "omeda_client.hpp"

#include <memory>
#include <string>
#include <vector>

namespace predeye {

// Wynik komendy "build" — bez formatowania, gotowy do prezentacji.
struct AdvisorBuild {
    std::string hero_name;
    std::string role_name;
    BuildResult build;
};

// Wynik komendy "counter" — profil wroga + counter-build + pominieci bohaterowie.
struct AdvisorCounter {
    std::string hero_name;
    std::string role_name;
    std::vector<EnemyBuildProfile> enemy_builds;
    std::vector<std::string> skipped_enemies; // brak buildow spolecznosci
    EnemyProfile profile;
    BuildResult counter;
};

// Sugestie draftu dla kreatora: bany i picki w jednym wyniku.
struct AdvisorDraft {
    std::vector<DraftSuggestion> bans;
    std::vector<DraftSuggestion> picks;
};

// Fasada trzymajaca klienta API i leniwie ladowane dane (heroes/items).
// Metody rzucaja wyjatkiem (OmedaError / runtime_error) — warstwa UI lapie.
class Advisor {
  public:
    explicit Advisor(std::string cache_dir = OmedaClient::default_dir());

    // Lista nazw bohaterow (display_name, posortowana) — do listy wyboru w GUI.
    std::vector<std::string> hero_names();

    AdvisorBuild build(const std::string& hero, Role role);
    AdvisorCounter counter(const std::string& hero, Role role,
                           const std::vector<std::string>& enemies);

    // Kreator rankedowy: sugestie banow (excluded = juz zbanowani/wybrani)
    // i pickow z puli (data/hero_pool.json) przeciw widocznym pickom wroga.
    // Meta pobierana leniwie z API (cache TTL 6 h).
    AdvisorDraft draft(const std::vector<std::string>& enemy_picks,
                       const std::vector<std::string>& excluded);

    // Crest + skill order z najlepszego buildu spolecznosci, eternalsy
    // z data/eternals.json. Wskazniki eternali zyja tak dlugo jak Advisor.
    LoadoutAdvice loadout(const std::string& hero, Role role);

    // Reczne dane wlasciciela (pula, kontry, spolszczenie) — do statusu w GUI.
    const LocalData& local() const { return local_; }

  private:
    void ensure_loaded(); // leniwe pobranie heroes+items (sieciowe)

    OmedaClient api_;
    LocalData local_;
    std::unique_ptr<HeroDB> heroes_;
    std::vector<Item> items_;
    ItemIndex index_;
    std::unique_ptr<BuildEngine> engine_;
    std::unique_ptr<DraftAdvisor> draft_advisor_; // leniwie (potrzebuje mety)
    bool loaded_ = false;
};

} // namespace predeye
