// core/draft_advisor — sugestie banow i pickow do rankedow.
// Zrodla: meta globalna z API (/dashboard/hero_statistics.json), reczna baza
// kontr (data/counters.json) i pula bohaterow wlasciciela (data/hero_pool.json).
// API nie ma matchupow 1v1 — kontry sa wiedza wlasciciela, meta podpowiada sile.
#pragma once

#include "hero_context.hpp"
#include "local_data.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace predeye {

// Znormalizowany wiersz meta jednego bohatera.
struct MetaHeroStat {
    long long hero_id = 0;
    std::string name;      // z HeroDB (API daje "Unknown")
    double winrate = 0.0;  // 0-1 (API daje 0-100 — §5)
    double pickrate = 0.0; // 0-1
    double matches = 0.0;
};

// Parsuje /dashboard/hero_statistics.json: normalizacja skal, nazwy po
// hero_id z HeroDB, odfiltrowanie wiersza-agregatu (id spoza HeroDB).
std::vector<MetaHeroStat> parse_meta(const nlohmann::json& meta_json, const HeroDB& heroes);

struct DraftSuggestion {
    std::string hero;
    double score = 0.0;
    std::string reason_pl; // uzasadnienie do pokazania w kreatorze
};

class DraftAdvisor {
  public:
    // `local` i `heroes` musza zyc dluzej niz advisor (trzymamy referencje).
    DraftAdvisor(const HeroDB& heroes, const LocalData& local, std::vector<MetaHeroStat> meta);

    // Sugestie banow: najgrozniejsi wg mety (winrate x popularnosc), z bonusem
    // za kontrowanie bohaterow z mojej puli (counters.json). `excluded` =
    // bohaterowie juz zbanowani/wybrani (pomijani).
    std::vector<DraftSuggestion> suggest_bans(const std::vector<std::string>& excluded,
                                              int count = 5) const;

    // Sugestie pickow Z MOJEJ PULI przeciw widocznym pickom wroga: forma
    // z mety + kontry z counters.json (w obie strony) + bilans obrazen
    // skladu wroga (latwiej itemizowac obrone przeciw miksowi).
    std::vector<DraftSuggestion> suggest_picks(const std::vector<std::string>& enemy_picks,
                                               const std::vector<std::string>& excluded,
                                               int count = 5) const;

    const std::vector<MetaHeroStat>& meta() const { return meta_; }

  private:
    const MetaHeroStat* meta_for(const std::string& hero) const;

    const HeroDB& heroes_;
    const LocalData& local_;
    std::vector<MetaHeroStat> meta_;
};

} // namespace predeye
