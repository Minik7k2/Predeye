// Sanity silnika (§9) + golden counter — na fixtures, bez sieci.
#include "core/build_engine.hpp"
#include "core/hero_context.hpp"
#include "fixtures.hpp"

#include <doctest/doctest.h>
#include <set>

using namespace predeye;

namespace {

// const char* zamiast std::string — unika tymczasowego, na ktory GCC 13
// falszywie raportuje -Wdangling-reference.
const Item& find_item(const std::vector<Item>& items, const char* name) {
    for (const auto& it : items)
        if (it.display_name == name)
            return it;
    throw std::runtime_error(std::string("brak itemu w fixture: ") + name);
}

// Wagi mid-maga z tabeli sanity §9.
std::map<std::string, double> mid_mage_weights() {
    return {{"magical_power", 1.0}, {"magical_penetration", 2.5}, {"ability_haste", 0.6}};
}

} // namespace

TEST_CASE("sanity: score i efficiency wg tabeli §9") {
    const auto items = parse_items(load_fixture("items.json"));
    BuildEngine engine(items);
    const auto w = mid_mage_weights();

    struct Row {
        const char* name;
        int price;
        double score, eff_per_1000;
    };
    // Wartosci przeliczone na biezacym fixture (zgodne z tabela §9).
    const Row rows[] = {
        {"Wraith Leggings", 3100, 127.5, 41.13},
        {"Amulet Of Chaos", 3000, 122.5, 40.83},
        {"Diffusal Cane", 1200, 47.5, 39.58},
        {"Scepter", 850, 30.0, 35.29},
    };
    for (const auto& r : rows) {
        const auto& it = find_item(items, r.name);
        CHECK(it.total_price == r.price);
        const double s = engine.score(it, w);
        CHECK(s == doctest::Approx(r.score));
        CHECK(1000.0 * s / it.total_price == doctest::Approx(r.eff_per_1000).epsilon(0.001));
    }
}

TEST_CASE("sanity: wagi countera (pen 3.1, arF 0.6, arM 0.2)") {
    const auto items = parse_items(load_fixture("items.json"));
    BuildEngine engine(items);
    // physical_ratio 0.75 + has_tanks => arF 0.6, arM 0.2, pen 2.5+0.6=3.1.
    auto w = mid_mage_weights();
    w["physical_armor"] += 0.8 * 0.75;
    w["magical_armor"] += 0.8 * 0.25;
    w["magical_penetration"] += 0.6;
    w["physical_penetration"] += 0.6;

    CHECK(engine.score(find_item(items, "Wraith Leggings"), w) == doctest::Approx(134.1));
    const auto& golem = find_item(items, "Golem's Gift");
    CHECK(engine.score(golem, mid_mage_weights()) == doctest::Approx(84.0));
    CHECK(engine.score(golem, w) == doctest::Approx(108.0));
}

TEST_CASE("golden counter: The Fey mid vs heal+crit+tank") {
    const auto items = parse_items(load_fixture("items.json"));
    const HeroDB db(load_fixture("heroes.json"));

    const auto* fey = db.by_name("The Fey");
    REQUIRE(fey != nullptr);
    const auto enemies = db.by_names({"Sparrow", "Muriel", "Steel", "Grux", "Kallari"});
    const auto profile = enemy_from(enemies);
    REQUIRE(profile.has_healing);
    REQUIRE(profile.has_crit);
    REQUIRE(profile.has_tanks);

    BuildEngine engine(items);
    const auto obj = objective_for(*fey, Role::Midlane);
    const auto res = engine.counter_build(obj, profile);

    int anti_heal = 0, anti_tank = 0, crests = 0;
    std::set<long long> ids;
    for (const auto& p : res.items) {
        if (p.item.aggression_type == "AntiHeal")
            ++anti_heal;
        if (p.item.aggression_type == "AntiTank")
            ++anti_tank;
        if (p.item.slot_type == "Crest")
            ++crests;
        CHECK(ids.insert(p.item.id).second); // brak duplikatow
        CHECK(p.item.buyable());
    }
    CHECK(anti_heal >= 1);
    CHECK(anti_tank >= 1);
    CHECK(res.total_cost <= obj.budget);
    CHECK(crests <= 1);
    CHECK(static_cast<int>(res.items.size()) <= obj.max_items);
}

TEST_CASE("optimize: respektuje budzet, max_items i sito klasowe") {
    const auto items = parse_items(load_fixture("items.json"));
    const HeroDB db(load_fixture("heroes.json"));
    BuildEngine engine(items);

    const auto obj = objective_for(*db.by_name("The Fey"), Role::Midlane);
    const auto res = engine.optimize(obj);
    REQUIRE(!res.items.empty());
    CHECK(res.total_cost <= obj.budget);
    CHECK(static_cast<int>(res.items.size()) <= obj.max_items);
    for (const auto& p : res.items) {
        CHECK(p.score > 0.0);
        const bool class_ok = p.item.hero_class.empty() || p.item.hero_class == "None" ||
                              p.item.hero_class == "Mage";
        CHECK(class_ok);
        const bool slot_ok = p.item.slot_type == "Passive" || p.item.slot_type == "Crest";
        CHECK(slot_ok);
    }
}
