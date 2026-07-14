// core/advisor — implementacja fasady (patrz advisor.hpp).
#include "core/advisor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace predeye {

Advisor::Advisor(std::string cache_dir) : api_(std::move(cache_dir)) {}

void Advisor::ensure_loaded() {
    if (loaded_)
        return;
    // Kolejnosc: bohaterowie (do rozpoznania nazw) + itemy (do silnika).
    heroes_ = std::make_unique<HeroDB>(api_.heroes());
    items_ = parse_items(api_.items());
    index_ = index_by_id(items_);
    engine_ = std::make_unique<BuildEngine>(items_);
    loaded_ = true;
}

std::vector<std::string> Advisor::hero_names() {
    ensure_loaded();
    std::vector<std::string> names;
    names.reserve(heroes_->all().size());
    for (const auto& h : heroes_->all())
        names.push_back(h.name);
    std::sort(names.begin(), names.end());
    return names;
}

AdvisorBuild Advisor::build(const std::string& hero, Role role) {
    ensure_loaded();
    const auto me = heroes_->by_names({hero}).front(); // rzuca gdy nieznany
    AdvisorBuild out;
    out.hero_name = me.name;
    out.role_name = role_name(role);
    out.build = engine_->optimize(objective_for(me, role));
    return out;
}

AdvisorDraft Advisor::draft(const std::vector<std::string>& enemy_picks,
                            const std::vector<std::string>& excluded) {
    ensure_loaded();
    if (!draft_advisor_)
        draft_advisor_ = std::make_unique<DraftAdvisor>(*heroes_, local_,
                                                        parse_meta(api_.meta(), *heroes_));
    AdvisorDraft out;
    out.bans = draft_advisor_->suggest_bans(excluded);
    out.picks = draft_advisor_->suggest_picks(enemy_picks, excluded);
    return out;
}

LoadoutAdvice Advisor::loadout(const std::string& hero, Role role) {
    ensure_loaded();
    const auto me = heroes_->by_names({hero}).front(); // rzuca gdy nieznany
    return loadout_for(api_, index_, local_, me, role);
}

AdvisorCounter Advisor::counter(const std::string& hero, Role role,
                                const std::vector<std::string>& enemies) {
    ensure_loaded();
    const auto me = heroes_->by_names({hero}).front();
    const auto enemy_profiles = heroes_->by_names(enemies); // rzuca gdy nieznany

    AdvisorCounter out;
    out.hero_name = me.name;
    out.role_name = role_name(role);

    // Zgrubny profil z klas, doostrzony typowymi buildami z API.
    out.profile = enemy_from(enemy_profiles);
    for (const auto& e : enemy_profiles) {
        auto tb = typical_build(api_, index_, e.id, e.name);
        if (!tb) {
            out.skipped_enemies.push_back(e.name);
            continue;
        }
        out.enemy_builds.push_back(std::move(*tb));
    }
    refine_enemy_profile(out.profile, out.enemy_builds);

    out.counter = engine_->counter_build(objective_for(me, role), out.profile);
    return out;
}

} // namespace predeye
