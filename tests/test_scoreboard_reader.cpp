// Testy scoreboard_reader — offline, syntetyczny scoreboard (bez sieci, §9).
#include "core/models.hpp"
#include "vision/scoreboard_reader.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <random>

namespace fs = std::filesystem;
using namespace predeye;

namespace {

// Deterministyczna "ikona" — kolorowy wzor o wyraznej strukturze (jak w matcherze).
cv::Mat synth_icon(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    cv::Mat icon(64, 64, CV_8UC3);
    for (int by = 0; by < 8; ++by)
        for (int bx = 0; bx < 8; ++bx)
            cv::rectangle(icon, {bx * 8, by * 8, 8, 8},
                          cv::Scalar(d(rng), d(rng), d(rng)), cv::FILLED);
    return icon;
}

// Baza ikon 1..N w katalogu tymczasowym + manifest — offline IconMatcher.
std::string make_icon_dir() {
    const fs::path dir = fs::temp_directory_path() / "predeye_test_sb_icons";
    fs::create_directories(dir);
    std::string manifest = "{";
    for (long long id = 1; id <= 5; ++id) {
        const std::string file = std::to_string(id) + ".png";
        cv::imwrite((dir / file).string(), synth_icon(static_cast<uint32_t>(id)));
        manifest += (id > 1 ? "," : "") + std::string("\"") + std::to_string(id) + "\":\"" +
                    file + "\"";
    }
    manifest += "}";
    std::ofstream(dir / "manifest.json", std::ios::trunc) << manifest;
    return dir.string();
}

Item mk_item(long long id, const std::string& name) {
    Item it;
    it.id = id;
    it.display_name = name;
    return it;
}

// Indeks itemow zgodny z ikonami 1..5.
ItemIndex make_index() {
    ItemIndex idx;
    for (long long id = 1; id <= 5; ++id)
        idx[id] = mk_item(id, "Item" + std::to_string(id));
    return idx;
}

// Siatka wroga w klatce; siatka sojusznikow domyslnie DALEKO poza klatka
// (jej sloty czytaja sie jako puste) — testy wybieraja, ktora wypelnic.
Calibration make_calib(const cv::Size& res) {
    Calibration c;
    c.resolution = res;
    c.enemy_item_grid.origin = {10, 10};
    c.enemy_item_grid.slot = {48, 48};
    c.enemy_item_grid.dx = 58;
    c.enemy_item_grid.dy = 58;
    c.enemy_item_grid.cols = 3;
    c.enemy_item_grid.rows = 2;
    c.ally_item_grid = c.enemy_item_grid;
    c.ally_item_grid.origin = {5000, 5000};
    return c;
}

// Wkleja ikone (przeskalowana do slotu) w prostokat slotu.
void paste_icon(cv::Mat& frame, const cv::Rect& r, uint32_t seed) {
    cv::Mat resized;
    cv::resize(synth_icon(seed), resized, r.size(), 0, 0, cv::INTER_AREA);
    resized.copyTo(frame(r));
}

} // namespace

TEST_CASE("read_scoreboard: rozpoznaje itemy wroga, odrzuca puste sloty") {
    const IconMatcher matcher(make_icon_dir());
    REQUIRE(matcher.base_size() == 5);
    const ItemIndex index = make_index();
    const Calibration calib = make_calib({240, 160});

    // Ciemne tlo -> puste sloty; w wybrane sloty wklejamy ikony.
    cv::Mat frame(160, 240, CV_8UC3, cv::Scalar(10, 10, 12));
    const GridSpec& g = calib.enemy_item_grid;
    paste_icon(frame, g.slot_rect(0, 0), 1); // wrog 1: Item1, Item2
    paste_icon(frame, g.slot_rect(0, 1), 2);
    paste_icon(frame, g.slot_rect(1, 2), 3); // wrog 2: Item3 (w ostatnim slocie)

    const ScoreboardRead read = read_scoreboard(frame, calib, matcher, index);

    REQUIRE(read.enemies.size() == 2);
    CHECK(read.total_items == 3);

    // Wrog 1: dwa itemy w kolejnosci slotow.
    REQUIRE(read.enemies[0].items.size() == 2);
    CHECK(read.enemies[0].items[0].id == 1);
    CHECK(read.enemies[0].items[1].id == 2);

    // Wrog 2: jeden item; puste sloty pominiete.
    REQUIRE(read.enemies[1].items.size() == 1);
    CHECK(read.enemies[1].items[0].id == 3);

    // Puste sloty sa oznaczone jako empty.
    int empty_count = 0;
    for (const auto& e : read.enemies)
        for (const auto& s : e.slots)
            if (s.empty)
                ++empty_count;
    CHECK(empty_count == 3); // 6 slotow - 3 z ikonami

    // Siatka sojusznikow poza klatka -> wiersze istnieja, ale puste.
    REQUIRE(read.allies.size() == 2);
    for (const auto& a : read.allies)
        CHECK(a.items.empty());
}

TEST_CASE("read_scoreboard: czyta tez siatke sojusznikow") {
    const IconMatcher matcher(make_icon_dir());
    const ItemIndex index = make_index();
    Calibration calib = make_calib({240, 160});
    // Zamiana rol siatek: wrogowie poza klatka, sojusznicy w klatce.
    calib.ally_item_grid.origin = calib.enemy_item_grid.origin;
    calib.enemy_item_grid.origin = {5000, 5000};

    cv::Mat frame(160, 240, CV_8UC3, cv::Scalar(10, 10, 12));
    paste_icon(frame, calib.ally_item_grid.slot_rect(0, 0), 4);
    paste_icon(frame, calib.ally_item_grid.slot_rect(1, 1), 5);

    const ScoreboardRead read = read_scoreboard(frame, calib, matcher, index);
    CHECK(read.total_items == 2);
    REQUIRE(read.allies.size() == 2);
    REQUIRE(read.allies[0].items.size() == 1);
    CHECK(read.allies[0].items[0].id == 4);
    REQUIRE(read.allies[1].items.size() == 1);
    CHECK(read.allies[1].items[0].id == 5);
    for (const auto& e : read.enemies)
        CHECK(e.items.empty());
}

TEST_CASE("read_scoreboard: ROI poza klatka traktowane jak puste, nie rzuca") {
    const IconMatcher matcher(make_icon_dir());
    const ItemIndex index = make_index();
    Calibration calib = make_calib({240, 160});
    // Origin daleko poza klatka -> wszystkie sloty wypadaja poza obraz.
    calib.enemy_item_grid.origin = {5000, 5000};

    cv::Mat frame(160, 240, CV_8UC3, cv::Scalar(10, 10, 12));
    ScoreboardRead read;
    REQUIRE_NOTHROW(read = read_scoreboard(frame, calib, matcher, index));
    CHECK(read.total_items == 0);
    for (const auto& e : read.enemies)
        CHECK(e.items.empty());
}

TEST_CASE("scoreboard_row_role: staly porzadek rol dla 5 wierszy, inaczej Unknown") {
    CHECK(scoreboard_row_role(0, 5) == Role::Offlane);
    CHECK(scoreboard_row_role(1, 5) == Role::Jungle);
    CHECK(scoreboard_row_role(2, 5) == Role::Midlane);
    CHECK(scoreboard_row_role(3, 5) == Role::Carry);
    CHECK(scoreboard_row_role(4, 5) == Role::Support);
    CHECK(scoreboard_row_role(0, 4) == Role::Unknown);
    CHECK(scoreboard_row_role(5, 5) == Role::Unknown);
    CHECK(scoreboard_row_role(-1, 5) == Role::Unknown);
}
