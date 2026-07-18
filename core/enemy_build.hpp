// core/enemy_build — typowe buildy wrogow z API i klasyfikacja itemizacji.
#pragma once

#include "build_engine.hpp"
#include "models.hpp"
#include "omeda_client.hpp"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace predeye {

struct EnemyBuildProfile {
    long long hero_id = 0;
    std::string hero_name, title;
    std::vector<Item> items;
    double magical_share = 0.5; // z sumy power-statow
    int crit_items = 0, sustain_items = 0, armor_items = 0, attack_speed_items = 0;

    bool crit_heavy() const { return crit_items >= 2; }
    bool sustain_heavy() const { return sustain_items >= 1; }
    bool tanky() const { return armor_items >= 2; }
    bool is_magical() const { return magical_share > 0.6; }
};

// Wspolna logika klasyfikacji — reuzywana w M4 dla itemow ze scoreboardu.
EnemyBuildProfile classify_items(const std::vector<Item>& items);

// Surowy rekord najlepszego buildu (max upvotes-downvotes) z JSON-a buildow —
// zawiera tez crest_id, skill_order i modules (uzywa loadout/shopping advisor).
std::optional<nlohmann::json> best_build_record(const nlohmann::json& builds_json);

// Najlepszy build (max upvotes-downvotes) z gotowego JSON-a buildow.
std::optional<EnemyBuildProfile> typical_build_from_json(const nlohmann::json& builds_json,
                                                         const ItemIndex& index,
                                                         long long hero_id,
                                                         const std::string& hero_name);

// Jak wyzej, ale pobiera buildy z API (rola pusta = dowolna);
// brak buildow => nullopt (pomin bohatera, nie wywalaj sie).
std::optional<EnemyBuildProfile> typical_build(OmedaClient& api, const ItemIndex& index,
                                               long long hero_id, const std::string& hero_name);

// Doostrzenie profilu wroga realnymi buildami: ratio z magical_share, flagi OR.
void refine_enemy_profile(EnemyProfile& profile,
                          const std::vector<EnemyBuildProfile>& builds);

} // namespace predeye
