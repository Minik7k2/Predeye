#include "enemy_build.hpp"

#include <nlohmann/json.hpp>

namespace predeye {

EnemyBuildProfile classify_items(const std::vector<Item>& items) {
    EnemyBuildProfile p;
    p.items = items;
    double phys = 0.0, mag = 0.0;
    for (const auto& it : items) {
        phys += it.stat("physical_power");
        mag += it.stat("magical_power");
        if (it.stat("critical_chance") > 0)
            ++p.crit_items;
        if (it.stat("lifesteal") > 0 || it.stat("omnivamp") > 0 ||
            it.stat("magical_lifesteal") > 0 || it.stat("heal_shield_increase") > 0)
            ++p.sustain_items;
        if (it.stat("physical_armor") > 0 || it.stat("magical_armor") > 0 ||
            it.stat("max_health") > 0)
            ++p.armor_items;
        if (it.stat("attack_speed") > 0)
            ++p.attack_speed_items;
    }
    p.magical_share = (phys + mag) > 0 ? mag / (phys + mag) : 0.5;
    return p;
}

std::optional<EnemyBuildProfile> typical_build_from_json(const nlohmann::json& builds_json,
                                                         const ItemIndex& index,
                                                         long long hero_id,
                                                         const std::string& hero_name) {
    // API zwraca liste; defensywnie obsluz tez obiekt z kluczem "builds".
    const nlohmann::json* arr = &builds_json;
    if (builds_json.is_object() && builds_json.contains("builds"))
        arr = &builds_json["builds"];
    if (!arr->is_array() || arr->empty())
        return std::nullopt;

    const nlohmann::json* best = nullptr;
    long long best_votes = 0;
    for (const auto& b : *arr) {
        if (!b.is_object())
            continue;
        long long votes = jll(b, "upvotes_count") - jll(b, "downvotes_count");
        if (!best || votes > best_votes) {
            best = &b;
            best_votes = votes;
        }
    }
    if (!best)
        return std::nullopt;

    std::vector<Item> items;
    for (int i = 1; i <= 6; ++i) {
        long long id = jll(*best, "item" + std::to_string(i) + "_id");
        auto it = index.find(id);
        if (id > 0 && it != index.end())
            items.push_back(it->second);
    }
    EnemyBuildProfile p = classify_items(items);
    p.hero_id = hero_id;
    p.hero_name = hero_name;
    p.title = jstr(*best, "title");
    return p;
}

std::optional<EnemyBuildProfile> typical_build(OmedaClient& api, const ItemIndex& index,
                                               long long hero_id, const std::string& hero_name) {
    try {
        return typical_build_from_json(api.builds(hero_id, ""), index, hero_id, hero_name);
    } catch (const OmedaError&) {
        // Blad sieci dla jednego bohatera nie moze wywracac calej analizy.
        return std::nullopt;
    }
}

void refine_enemy_profile(EnemyProfile& profile, const std::vector<EnemyBuildProfile>& builds) {
    if (builds.empty())
        return;
    double sum = 0.0;
    for (const auto& b : builds) {
        sum += 1.0 - b.magical_share;
        profile.has_crit = profile.has_crit || b.crit_heavy();
        profile.has_healing = profile.has_healing || b.sustain_heavy();
        profile.has_tanks = profile.has_tanks || b.tanky();
    }
    profile.physical_ratio = sum / static_cast<double>(builds.size());
}

} // namespace predeye
