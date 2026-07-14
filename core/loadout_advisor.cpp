#include "loadout_advisor.hpp"

#include "enemy_build.hpp"

#include <nlohmann/json.hpp>

namespace predeye {

LoadoutAdvice loadout_from_json(const nlohmann::json& builds_json, const ItemIndex& index,
                                const LocalData& local, const std::string& hero_name, Role role) {
    LoadoutAdvice out;
    out.hero_name = hero_name;
    out.eternals = local.eternals_for(hero_name, role_name(role)); // role_name juz lowercase

    const auto best = best_build_record(builds_json);
    if (!best)
        return out;

    out.build_title = jstr(*best, "title");

    const long long crest_id = jll(*best, "crest_id");
    if (crest_id > 0) {
        const auto it = index.find(crest_id);
        if (it != index.end())
            out.crest = it->second;
    }

    if (best->contains("skill_order") && (*best)["skill_order"].is_array()) {
        for (const auto& s : (*best)["skill_order"])
            if (s.is_number())
                out.skill_order.push_back(s.get<int>());
    }
    return out;
}

LoadoutAdvice loadout_for(OmedaClient& api, const ItemIndex& index, const LocalData& local,
                          const HeroProfile& hero, Role role) {
    nlohmann::json builds = nlohmann::json::array();
    try {
        // Najpierw buildy dla konkretnej roli; pusto => dowolna rola.
        if (role != Role::Unknown)
            builds = api.builds(hero.id, role_name(role));
        if (!builds.is_array() || builds.empty())
            builds = api.builds(hero.id, "");
    } catch (const OmedaError&) {
        // Bez sieci zostaja eternalsy z bazy lokalnej.
    }
    return loadout_from_json(builds, index, local, hero.name, role);
}

} // namespace predeye
