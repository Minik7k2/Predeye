// Testy parsera itemow — pulapki z §5 (null-e, total_price==0, duze id).
#include "core/models.hpp"
#include "fixtures.hpp"

#include <doctest/doctest.h>

using namespace predeye;

TEST_CASE("parse_items: pelny fixture items.json") {
    const auto items = parse_items(load_fixture("items.json"));
    CHECK(items.size() >= 200);

    int buyable = 0;
    for (const auto& it : items)
        if (it.buyable())
            ++buyable;
    // ~213 kupowalnych wg spec; nie przybijamy dokladnej liczby (patche).
    CHECK(buyable >= 150);
    CHECK(buyable < static_cast<int>(items.size()));
}

TEST_CASE("parse_items: pola null / brakujace nie wywracaja parsera") {
    const auto j = nlohmann::json::parse(R"([
      {"id": 1, "display_name": "Nullowy", "total_price": null,
       "hero_class": null, "aggression_type": null, "required_level": null,
       "stats": {"physical_power": 10, "junk": null}},
      {"id": 10000000001, "display_name": "Wielkie ID", "total_price": 500, "stats": {}},
      "smiec-nie-obiekt"
    ])");
    const auto items = parse_items(j);
    REQUIRE(items.size() == 2);

    CHECK(items[0].total_price == 0); // null => 0 => niekupowalny
    CHECK_FALSE(items[0].buyable());
    CHECK(items[0].hero_class.empty());
    CHECK(items[0].stat("physical_power") == doctest::Approx(10.0));
    CHECK(items[0].stat("junk") == doctest::Approx(0.0));      // null pominiety
    CHECK(items[0].stat("nie_ma") == doctest::Approx(0.0));    // brak => 0.0

    CHECK(items[1].id == 10000000001LL); // id poza int32 (Yurei-case)
    CHECK(items[1].buyable());
}

TEST_CASE("index_by_id: wyszukiwanie po id") {
    const auto items = parse_items(load_fixture("items.json"));
    const auto idx = index_by_id(items);
    CHECK(idx.size() == items.size());
    // Wraith Leggings — item z tabeli sanity §9.
    bool found = false;
    for (const auto& [id, it] : idx)
        if (it.display_name == "Wraith Leggings") {
            found = true;
            CHECK(id == it.id);
            CHECK(it.total_price == 3100);
        }
    CHECK(found);
}
