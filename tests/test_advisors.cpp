// Testy advisory rankedowych (draft/loadout/shopping) — fixtures, bez sieci (§9).
#include "core/draft_advisor.hpp"
#include "core/loadout_advisor.hpp"
#include "core/shopping_advisor.hpp"

#include "fixtures.hpp"

#include <doctest/doctest.h>

using namespace predeye;

namespace {
const std::string kDataDir = std::string(PREDEYE_FIXTURES_DIR) + "/data";
}

TEST_CASE("parse_meta: normalizacja skal, nazwy z HeroDB, agregat odfiltrowany") {
    const HeroDB heroes(load_fixture("heroes.json"));
    const auto meta = parse_meta(load_fixture("meta_hero_stats.json"), heroes);

    // Fixture ma 10 wierszy, w tym wiersz-agregat hero_id=76 spoza HeroDB.
    CHECK(meta.size() == 9);
    for (const auto& m : meta) {
        CHECK(m.hero_id != 76);
        CHECK(!m.name.empty());
        CHECK(m.name != "Unknown");        // nazwa mapowana po id (§5)
        CHECK(m.winrate > 0.3);            // skala 0-1 po normalizacji
        CHECK(m.winrate < 0.7);
        CHECK(m.pickrate < 1.0);
    }
}

TEST_CASE("DraftAdvisor: bany wg mety, wykluczenia dzialaja") {
    const HeroDB heroes(load_fixture("heroes.json"));
    const LocalData local(kDataDir);
    const auto meta = parse_meta(load_fixture("meta_hero_stats.json"), heroes);
    const DraftAdvisor advisor(heroes, local, meta);

    const auto bans = advisor.suggest_bans({}, 3);
    REQUIRE(bans.size() == 3);
    // Malejaco po score, kazdy z uzasadnieniem.
    CHECK(bans[0].score >= bans[1].score);
    CHECK(bans[1].score >= bans[2].score);
    for (const auto& b : bans)
        CHECK(b.reason_pl.find("winrate") != std::string::npos);

    // Wykluczenie lidera przesuwa liste.
    const auto bans2 = advisor.suggest_bans({bans[0].hero}, 3);
    CHECK(bans2[0].hero == bans[1].hero);
}

TEST_CASE("DraftAdvisor: picki z puli, kontra podbija wynik") {
    const HeroDB heroes(load_fixture("heroes.json"));
    const LocalData local(kDataDir); // pula: the-fey, gideon; fey kontruje gideona
    const auto meta = parse_meta(load_fixture("meta_hero_stats.json"), heroes);
    const DraftAdvisor advisor(heroes, local, meta);

    // Wrog picknal Gideona -> The Fey (kontruje go wg counters.json) na czele.
    const auto picks = advisor.suggest_picks({"Gideon"}, {}, 5);
    REQUIRE(!picks.empty());
    CHECK(picks[0].hero == "The Fey");
    CHECK(picks[0].reason_pl.find("kontruje") != std::string::npos);
    // Gideon jest pickiem wroga — nie moze byc sugerowany.
    for (const auto& p : picks)
        CHECK(p.hero != "Gideon");

    // Wrog picknal Kallari (kontruje The Fey wg counters.json) -> malus i ostrzezenie.
    const auto picks2 = advisor.suggest_picks({"Kallari"}, {}, 5);
    bool warned = false;
    for (const auto& p : picks2)
        if (p.hero == "The Fey" && p.reason_pl.find("uwaga") != std::string::npos)
            warned = true;
    CHECK(warned);
}

TEST_CASE("loadout_from_json: crest, skill order i eternalsy z bazy lokalnej") {
    const HeroDB heroes(load_fixture("heroes.json"));
    const LocalData local(kDataDir);
    const auto items = parse_items(load_fixture("items.json"));
    const ItemIndex index = index_by_id(items);

    const LoadoutAdvice adv = loadout_from_json(load_fixture("builds_the_fey.json"), index,
                                                local, "The Fey", Role::Midlane);
    CHECK(adv.hero_name == "The Fey");
    CHECK(!adv.build_title.empty());
    // Najlepszy build (max upvotes-downvotes) w fixture ma crest_id=21
    // i 18-poziomowy skill_order.
    REQUIRE(adv.crest.has_value());
    CHECK(adv.crest->id == 21);
    REQUIRE(adv.skill_order.size() == 18);
    for (int s : adv.skill_order) {
        CHECK(s >= 1);
        CHECK(s <= 4);
    }
    // Eternal z fixture: focus-primary pasuje do (the-fey, midlane).
    REQUIRE(adv.eternals.size() == 1);
    CHECK(adv.eternals[0]->id == "focus-primary");

    // Pusty JSON buildow: zostaja same eternalsy, bez crestu.
    const LoadoutAdvice empty = loadout_from_json(nlohmann::json::array(), index, local,
                                                  "The Fey", Role::Midlane);
    CHECK(!empty.crest.has_value());
    CHECK(empty.skill_order.empty());
    CHECK(empty.eternals.size() == 1);
}

TEST_CASE("next_purchases: odejmuje posiadane, tlumaczy powody na polski") {
    const LocalData local(kDataDir);

    // Counter-build z dwoma itemami: counter AntiHeal + zwykly (efektywnosc).
    BuildResult counter;
    PickedItem p1;
    p1.item.id = 168;
    p1.item.slug = "tainted-scepter";
    p1.item.display_name = "Tainted Scepter";
    p1.item.total_price = 3000;
    p1.reason = "counter: AntiHeal (magical_power 75, ability_haste 10)";
    PickedItem p2;
    p2.item.id = 1;
    p2.item.slug = "wraith-leggings";
    p2.item.display_name = "Wraith Leggings";
    p2.item.total_price = 3100;
    p2.reason = "magical_power 100, magical_penetration 11";
    counter.items = {p1, p2};

    // Mamy juz Wraith Leggings -> w kolejce zostaje tylko Tainted Scepter.
    const ShoppingAdvice adv = next_purchases(counter, {p2.item}, local);
    REQUIRE(adv.queue.size() == 1);
    CHECK(adv.queue[0].item.id == 168);
    CHECK(adv.remaining_cost == 3000);
    REQUIRE(adv.owned_in_plan.size() == 1);
    CHECK(adv.owned_in_plan[0].id == 1);

    // Powod po polsku: tag przetlumaczony + spolszczenie z pl_items.json.
    const std::string& r = adv.queue[0].reason_pl;
    CHECK(r.find("kontra na leczenie") != std::string::npos);
    CHECK(r.find("moc magiczna") != std::string::npos);
    CHECK(r.find("AntiHeal") == std::string::npos); // brak surowego tagu
    CHECK(r.find("Blighted Spells") != std::string::npos); // efekt z pl_items
}
