#include "hero_context.hpp"

#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>

namespace predeye {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool has_class(const std::vector<std::string>& classes, const char* name) {
    return std::find(classes.begin(), classes.end(), name) != classes.end();
}

// Wyprowadzenie typu obrazen z klas (WIAZACE §6.3) — base_stats sa bezuzyteczne.
DamageType damage_from_classes(const std::vector<std::string>& c) {
    if (has_class(c, "Mage"))
        return DamageType::Magical;
    if (has_class(c, "Sharpshooter") || has_class(c, "Assassin") || has_class(c, "Fighter") ||
        has_class(c, "Executioner"))
        return DamageType::Physical;
    if (has_class(c, "Enchanter") || has_class(c, "Support") || has_class(c, "Catcher"))
        return DamageType::Mixed;
    return DamageType::Physical; // reszta (Tank/Warden itd.)
}

size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    std::iota(prev.begin(), prev.end(), size_t{0});
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

} // namespace

Role parse_role(const std::string& s) {
    const std::string r = lower(s);
    if (r == "offlane" || r == "off" || r == "solo")
        return Role::Offlane;
    if (r == "jungle" || r == "jungler" || r == "jg")
        return Role::Jungle;
    if (r == "midlane" || r == "mid")
        return Role::Midlane;
    if (r == "carry" || r == "adc" || r == "duo")
        return Role::Carry;
    if (r == "support" || r == "supp")
        return Role::Support;
    return Role::Unknown;
}

std::string role_name(Role r) {
    switch (r) {
    case Role::Offlane:
        return "offlane";
    case Role::Jungle:
        return "jungle";
    case Role::Midlane:
        return "midlane";
    case Role::Carry:
        return "carry";
    case Role::Support:
        return "support";
    default:
        return "unknown";
    }
}

HeroDB::HeroDB(const nlohmann::json& heroes_json) {
    if (!heroes_json.is_array())
        return;
    heroes_.reserve(heroes_json.size());
    for (const auto& h : heroes_json) {
        if (!h.is_object())
            continue;
        HeroProfile p;
        p.id = jll(h, "id");
        p.name = jstr(h, "display_name");
        p.slug = jstr(h, "slug");
        if (h.contains("classes") && h["classes"].is_array())
            for (const auto& c : h["classes"])
                if (c.is_string())
                    p.classes.push_back(c.get<std::string>());
        if (h.contains("roles") && h["roles"].is_array())
            for (const auto& r : h["roles"])
                if (r.is_string())
                    p.roles.push_back(r.get<std::string>());
        p.damage = damage_from_classes(p.classes);
        p.deals_crit = has_class(p.classes, "Sharpshooter");
        p.can_heal = has_class(p.classes, "Enchanter") || has_class(p.classes, "Support");
        p.is_tanky = has_class(p.classes, "Tank") || has_class(p.classes, "Warden");
        heroes_.push_back(std::move(p));
    }
}

const HeroProfile* HeroDB::by_name(const std::string& name) const {
    const std::string key = lower(name);
    for (const auto& h : heroes_)
        if (lower(h.name) == key || h.slug == key)
            return &h;
    return nullptr;
}

const HeroProfile* HeroDB::by_id(long long id) const {
    for (const auto& h : heroes_)
        if (h.id == id)
            return &h;
    return nullptr;
}

std::vector<std::string> HeroDB::suggest(const std::string& name, size_t count) const {
    const std::string key = lower(name);
    std::vector<std::pair<size_t, std::string>> ranked;
    for (const auto& h : heroes_)
        ranked.emplace_back(std::min(edit_distance(key, lower(h.name)),
                                     edit_distance(key, h.slug)),
                            h.name);
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < ranked.size() && i < count; ++i)
        out.push_back(ranked[i].second);
    return out;
}

std::vector<HeroProfile> HeroDB::by_names(const std::vector<std::string>& names) const {
    std::vector<HeroProfile> out;
    out.reserve(names.size());
    for (const auto& n : names) {
        const HeroProfile* p = by_name(n);
        if (!p) {
            std::string msg = "nieznany bohater: \"" + n + "\"; czy chodzilo o: ";
            auto sug = suggest(n);
            for (size_t i = 0; i < sug.size(); ++i)
                msg += (i ? ", " : "") + sug[i];
            msg += "?";
            throw std::runtime_error(msg);
        }
        out.push_back(*p);
    }
    return out;
}

Objective objective_for(const HeroProfile& me, Role role, int budget_override) {
    Objective obj;
    obj.name = me.name + " " + role_name(role);
    // power/pen = magical_* tylko dla czysto magicznych bohaterow (§6.3).
    const bool magical = me.damage == DamageType::Magical;
    const std::string power = magical ? "magical_power" : "physical_power";
    const std::string pen = magical ? "magical_penetration" : "physical_penetration";
    obj.hero_class = me.classes.empty() ? "" : me.classes.front();

    switch (role) {
    case Role::Carry:
        obj.weights = {{power, 1.0}, {"attack_speed", 0.8}, {"lifesteal", 0.4}, {pen, 1.0}};
        if (me.deals_crit)
            obj.weights["critical_chance"] = 1.2;
        obj.budget = 14000;
        break;
    case Role::Midlane:
        obj.weights = {{power, 1.0}, {pen, 2.5}, {"ability_haste", 0.6}};
        obj.budget = 12000;
        break;
    case Role::Jungle:
        obj.weights = {{power, 1.0}, {pen, 1.5}, {"ability_haste", 0.5}, {"omnivamp", 0.4}};
        obj.budget = 12000;
        break;
    case Role::Offlane:
    case Role::Unknown: // rozsadny domyslny preset
        obj.weights = {{power, 0.8}, {"max_health", 0.04}, {"ability_haste", 0.6}, {pen, 0.8}};
        obj.budget = 12000;
        break;
    case Role::Support:
        obj.weights = {{"heal_shield_increase", 1.5},
                       {"ability_haste", 1.0},
                       {"max_health", 0.04},
                       {"gold_per_second", 1.5},
                       {"tenacity", 0.4}};
        obj.budget = 9000;
        obj.hero_class = ""; // supportowe itemy bywaja poza klasa bohatera
        break;
    }
    if (budget_override > 0)
        obj.budget = budget_override;
    return obj;
}

EnemyProfile enemy_from(const std::vector<HeroProfile>& enemies) {
    EnemyProfile ep;
    if (enemies.empty())
        return ep;
    double sum = 0.0;
    for (const auto& e : enemies) {
        sum += e.damage == DamageType::Physical ? 1.0
               : e.damage == DamageType::Mixed  ? 0.5
                                                : 0.0;
        ep.has_healing = ep.has_healing || e.can_heal;
        ep.has_crit = ep.has_crit || e.deals_crit;
        ep.has_tanks = ep.has_tanks || e.is_tanky;
    }
    ep.physical_ratio = sum / static_cast<double>(enemies.size());
    return ep;
}

} // namespace predeye
