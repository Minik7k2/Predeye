#include "shopping_advisor.hpp"

#include <map>
#include <set>

namespace predeye {
namespace {

// Polskie nazwy tagow taktycznych (aggression_type z API).
const std::map<std::string, std::string> kTagPl = {
    {"AntiHeal", "kontra na leczenie wrogow"},
    {"AntiTank", "kontra na pancernych (przebicie)"},
    {"AntiMagic", "kontra na obrazenia magiczne"},
    {"AntiCrit", "kontra na trafienia krytyczne"},
    {"AntiBurst", "kontra na burst"},
    {"PhysicalShred", "kruszenie pancerza"},
    {"SpellShield", "tarcza na umiejetnosc"},
    {"Burst", "wiecej burstu"},
    {"Sustain", "sustain"},
    {"Mobility", "mobilnosc"},
    {"Offense", "ofensywa"},
    {"Defense", "obrona"},
    {"MultiKill", "nagroda za multi-kille"},
};

// Polskie nazwy statow (klucze stats z API) — do tlumaczenia `reason`.
const std::map<std::string, std::string> kStatPl = {
    {"physical_power", "moc fizyczna"},
    {"magical_power", "moc magiczna"},
    {"physical_penetration", "przebicie pancerza"},
    {"magical_penetration", "przebicie odpornosci mag."},
    {"attack_speed", "szybkosc ataku"},
    {"critical_chance", "szansa na kryt"},
    {"ability_haste", "przyspieszenie umiejetnosci"},
    {"lifesteal", "kradziez zycia"},
    {"omnivamp", "omniwamp"},
    {"magical_lifesteal", "magiczna kradziez zycia"},
    {"heal_shield_increase", "moc leczenia/tarcz"},
    {"max_health", "zdrowie"},
    {"physical_armor", "pancerz"},
    {"magical_armor", "odpornosc magiczna"},
    {"max_mana", "mana"},
    {"mana_regeneration", "regeneracja many"},
    {"health_regeneration", "regeneracja zdrowia"},
    {"gold_per_second", "zloto na sekunde"},
    {"tenacity", "nieustepliwosc"},
    {"movement_speed", "szybkosc ruchu"},
};

// Podmien angielskie klucze statow/tagow w tekscie powodu na polskie.
std::string translate_reason(std::string reason) {
    for (const auto& [en, pl] : kTagPl) {
        const auto pos = reason.find(en);
        if (pos != std::string::npos)
            reason.replace(pos, en.size(), pl);
    }
    for (const auto& [en, pl] : kStatPl) {
        for (size_t pos = reason.find(en); pos != std::string::npos; pos = reason.find(en, pos))
            reason.replace(pos, en.size(), pl);
    }
    const auto counter = reason.find("counter:");
    if (counter != std::string::npos)
        reason.replace(counter, 8, "counter —");
    return reason;
}

} // namespace

std::string purchase_reason_pl(const PickedItem& pick, const LocalData& local) {
    std::string out = translate_reason(pick.reason);
    if (const ItemPl* pl = local.item_pl(pick.item.slug)) {
        if (!pl->summary_pl.empty())
            out += ". " + pl->summary_pl;
        if (!pl->effects.empty()) {
            const std::string& eff = ItemPl::text(pl->effects.front());
            if (!eff.empty())
                out += " " + pl->effects.front().name + ": " + eff;
        }
    }
    return out;
}

ShoppingAdvice next_purchases(const BuildResult& counter, const std::vector<Item>& owned,
                              const LocalData& local) {
    ShoppingAdvice out;
    std::set<long long> owned_ids;
    for (const auto& it : owned)
        owned_ids.insert(it.id);

    for (const auto& pick : counter.items) {
        if (owned_ids.count(pick.item.id)) {
            out.owned_in_plan.push_back(pick.item);
            continue;
        }
        NextPurchase np;
        np.item = pick.item;
        np.reason_pl = purchase_reason_pl(pick, local);
        out.remaining_cost += pick.item.total_price;
        out.queue.push_back(std::move(np));
    }
    return out;
}

} // namespace predeye
