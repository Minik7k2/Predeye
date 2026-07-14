// Testy core/local_data: wczytywanie recznych danych z data/ + odpornosc
// na braki (brak pliku/wpisu nigdy nie przerywa programu).
#include "core/local_data.hpp"

#include <doctest/doctest.h>

using namespace predeye;

namespace {
const std::string kDataDir = std::string(PREDEYE_FIXTURES_DIR) + "/data";
}

TEST_CASE("local_data: spolszczenie itemow z fallbackiem PL -> EN") {
    LocalData ld(kDataDir);
    REQUIRE(ld.items_pl_count() == 1);

    const ItemPl* it = ld.item_pl("tainted-scepter");
    REQUIRE(it != nullptr);
    CHECK(it->name == "Tainted Scepter");
    CHECK(!it->summary_pl.empty());
    REQUIRE(it->effects.size() == 2);
    // Efekt z tlumaczeniem -> PL; bez tlumaczenia -> fallback EN.
    CHECK(ItemPl::text(it->effects[0]) == "Obniza leczenie celu o 45% na 4 s.");
    CHECK(ItemPl::text(it->effects[1]) == "Gain a stack of Malice every second.");

    CHECK(ld.item_pl("nie-ma-takiego") == nullptr);
}

TEST_CASE("local_data: kontry — dopasowanie case-insensitive, smieci pomijane") {
    LocalData ld(kDataDir);
    // Wpis bez bohatera pominiety; "The Fey" i "wraith" wczytane.
    CHECK(ld.counters_count() == 2);

    const CounterEntry* fey = ld.counter_entry("the fey");
    REQUIRE(fey != nullptr);
    CHECK(fey->counters == std::vector<std::string>{"gideon"});
    CHECK(fey->countered_by.size() == 2);
    CHECK(fey->note_pl == "Bez mobilnosci.");

    // Niestringi w listach i null-e sa pomijane bez wyjatku.
    const CounterEntry* wraith = ld.counter_entry("WRAITH");
    REQUIRE(wraith != nullptr);
    CHECK(wraith->counters == std::vector<std::string>{"zinx"});
    CHECK(wraith->countered_by.empty());

    CHECK(ld.counter_entry("nieznany") == nullptr);
}

TEST_CASE("local_data: eternalsy + rekomendacje per (bohater, rola)") {
    LocalData ld(kDataDir);
    // Wpis bez id pominiety.
    REQUIRE(ld.eternals().size() == 2);
    CHECK(ld.eternals()[0].id == "focus-primary");
    CHECK(ld.eternals()[0].slot == "primary");

    // Dopasowanie hero+rola.
    auto r = ld.eternals_for("The-Fey", "midlane");
    REQUIRE(r.size() == 1);
    CHECK(r[0]->name == "Focus");
    // Puste hero w rekomendacji = dowolny bohater tej roli.
    r = ld.eternals_for("murdock", "carry");
    REQUIRE(r.size() == 1);
    CHECK(r[0]->id == "focus-primary");
    // Brak dopasowania.
    CHECK(ld.eternals_for("murdock", "support").empty());
}

TEST_CASE("local_data: pula bohaterow") {
    LocalData ld(kDataDir);
    CHECK(ld.hero_pool().role == "midlane");
    CHECK(ld.hero_pool().heroes == std::vector<std::string>{"the-fey", "gideon"});
}

TEST_CASE("local_data: brak katalogu danych nie wywala programu") {
    LocalData ld("nie-ma-takiego-katalogu");
    CHECK(ld.items_pl_count() == 0);
    CHECK(ld.counters_count() == 0);
    CHECK(ld.eternals().empty());
    CHECK(ld.hero_pool().heroes.empty());
    CHECK(ld.item_pl("cokolwiek") == nullptr);
}
