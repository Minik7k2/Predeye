// Testy profili bohaterow (typ obrazen z klas — WIAZACE §6.3) i presetow.
#include "core/hero_context.hpp"
#include "fixtures.hpp"

#include <doctest/doctest.h>

using namespace predeye;

static HeroDB make_db() { return HeroDB(load_fixture("heroes.json")); }

TEST_CASE("HeroDB: lookup po display_name i slug, case-insensitive") {
    const auto db = make_db();
    const auto* fey = db.by_name("The Fey");
    REQUIRE(fey != nullptr);
    CHECK(db.by_name("the fey") == fey);
    CHECK(db.by_name("the-fey") == fey); // slug
    CHECK(db.by_name("GRIM.exe") != nullptr);
    CHECK(db.by_name("nie-ma-takiego") == nullptr);
}

TEST_CASE("HeroDB: by_names rzuca z podpowiedzia najblizszych nazw") {
    const auto db = make_db();
    try {
        db.by_names({"Wraif"});
        FAIL("oczekiwano wyjatku");
    } catch (const std::exception& ex) {
        const std::string msg = ex.what();
        CHECK(msg.find("Wraif") != std::string::npos);
        CHECK(msg.find("Wraith") != std::string::npos); // podpowiedz
    }
}

TEST_CASE("Typ obrazen z klas (WIAZACE)") {
    const auto db = make_db();
    CHECK(db.by_name("The Fey")->damage == DamageType::Magical);   // Mage
    CHECK(db.by_name("Wraith")->damage == DamageType::Physical);   // Sharpshooter
    CHECK(db.by_name("Zinx")->damage == DamageType::Mixed);        // Enchanter
    CHECK(db.by_name("Steel")->damage == DamageType::Physical);    // Tank => reszta
    CHECK(db.by_name("Wraith")->deals_crit);
    CHECK(db.by_name("Muriel")->can_heal);
    CHECK(db.by_name("Steel")->is_tanky);
    CHECK_FALSE(db.by_name("The Fey")->deals_crit);
}

TEST_CASE("Yurei: hero_id poza int32") {
    const auto db = make_db();
    const auto* yurei = db.by_name("Yurei");
    REQUIRE(yurei != nullptr);
    CHECK(yurei->id == 10000000001LL);
    CHECK(db.by_id(10000000001LL) == yurei);
}

TEST_CASE("objective_for: presety rol") {
    const auto db = make_db();
    const auto* fey = db.by_name("The Fey");

    const auto mid = objective_for(*fey, Role::Midlane);
    CHECK(mid.budget == 12000);
    CHECK(mid.hero_class == "Mage");
    CHECK(mid.weights.at("magical_power") == doctest::Approx(1.0));
    CHECK(mid.weights.at("magical_penetration") == doctest::Approx(2.5));
    CHECK(mid.weights.at("ability_haste") == doctest::Approx(0.6));

    const auto* wraith = db.by_name("Wraith");
    const auto carry = objective_for(*wraith, Role::Carry);
    CHECK(carry.budget == 14000);
    CHECK(carry.weights.at("physical_power") == doctest::Approx(1.0));
    CHECK(carry.weights.at("critical_chance") == doctest::Approx(1.2)); // Sharpshooter

    const auto supp = objective_for(*db.by_name("Muriel"), Role::Support);
    CHECK(supp.budget == 9000);
    CHECK(supp.hero_class.empty());
    CHECK(supp.weights.at("heal_shield_increase") == doctest::Approx(1.5));

    CHECK(objective_for(*fey, Role::Midlane, 7000).budget == 7000); // override
}

TEST_CASE("enemy_from: ratio i flagi OR") {
    const auto db = make_db();
    // Sparrow (SS, kryt) + Muriel (Enchanter, heal) + Steel (Tank) + 2x mag.
    const auto enemies =
        db.by_names({"Sparrow", "Muriel", "Steel", "The Fey", "Gideon"});
    const auto ep = enemy_from(enemies);
    // 1.0 + 0.5 + 1.0 + 0 + 0 => 2.5/5
    CHECK(ep.physical_ratio == doctest::Approx(0.5));
    CHECK(ep.has_crit);
    CHECK(ep.has_healing);
    CHECK(ep.has_tanks);
}
