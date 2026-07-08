// Testy klasyfikacji itemizacji wroga i typowych buildow (fixture builds.json).
#include "core/enemy_build.hpp"
#include "fixtures.hpp"

#include <doctest/doctest.h>

using namespace predeye;

namespace {

Item make_item(long long id, std::map<std::string, double> stats) {
    Item it;
    it.id = id;
    it.total_price = 1000;
    it.stats = std::move(stats);
    return it;
}

} // namespace

TEST_CASE("classify_items: liczniki i magical_share") {
    const std::vector<Item> items = {
        make_item(1, {{"physical_power", 40}, {"critical_chance", 20}}),
        make_item(2, {{"physical_power", 30}, {"critical_chance", 25}, {"attack_speed", 20}}),
        make_item(3, {{"physical_power", 30}, {"lifesteal", 10}}),
        make_item(4, {{"max_health", 300}, {"physical_armor", 40}}),
        make_item(5, {{"magical_armor", 40}, {"max_health", 250}}),
    };
    const auto p = classify_items(items);
    CHECK(p.crit_items == 2);
    CHECK(p.sustain_items == 1);
    CHECK(p.armor_items == 2);
    CHECK(p.attack_speed_items == 1);
    CHECK(p.crit_heavy());
    CHECK(p.sustain_heavy());
    CHECK(p.tanky());
    CHECK(p.magical_share == doctest::Approx(0.0));
    CHECK_FALSE(p.is_magical());
}

TEST_CASE("classify_items: pusta lista => share 0.5") {
    const auto p = classify_items({});
    CHECK(p.magical_share == doctest::Approx(0.5));
}

TEST_CASE("typical_build_from_json: wybiera max(upvotes-downvotes) i rozwija itemy") {
    const auto items = parse_items(load_fixture("items.json"));
    const auto index = index_by_id(items);
    const auto builds = load_fixture("builds_wraith.json");

    const auto p = typical_build_from_json(builds, index, 41, "Wraith");
    REQUIRE(p.has_value());
    CHECK(p->hero_id == 41);
    CHECK(p->hero_name == "Wraith");
    CHECK(!p->title.empty());
    CHECK(!p->items.empty());

    // Recznie znajdz build o max (up-down) i porownaj tytul.
    long long best_votes = -1;
    std::string best_title;
    for (const auto& b : builds) {
        long long v = b.value("upvotes_count", 0LL) - b.value("downvotes_count", 0LL);
        if (v > best_votes) {
            best_votes = v;
            best_title = b.value("title", std::string{});
        }
    }
    CHECK(p->title == best_title);
}

TEST_CASE("typical_build_from_json: brak buildow => nullopt") {
    const auto index = ItemIndex{};
    CHECK_FALSE(typical_build_from_json(nlohmann::json::array(), index, 1, "X").has_value());
    CHECK_FALSE(typical_build_from_json(nlohmann::json::object(), index, 1, "X").has_value());
}

TEST_CASE("refine_enemy_profile: ratio z buildow, flagi OR") {
    EnemyProfile ep; // domyslnie 0.5, flagi false
    EnemyBuildProfile magical;
    magical.magical_share = 0.9;
    EnemyBuildProfile crit;
    crit.magical_share = 0.1;
    crit.crit_items = 2;
    EnemyBuildProfile tank;
    tank.magical_share = 0.5;
    tank.armor_items = 3;
    tank.sustain_items = 1;

    refine_enemy_profile(ep, {magical, crit, tank});
    CHECK(ep.physical_ratio == doctest::Approx((0.1 + 0.9 + 0.5) / 3.0));
    CHECK(ep.has_crit);
    CHECK(ep.has_tanks);
    CHECK(ep.has_healing);

    // Puste buildy nie moga wyzerowac profilu.
    EnemyProfile ep2;
    ep2.has_crit = true;
    refine_enemy_profile(ep2, {});
    CHECK(ep2.has_crit);
    CHECK(ep2.physical_ratio == doctest::Approx(0.5));
}
