// Testy kalibracji: round-trip JSON, kompatybilnosc wsteczna, geometria
// siatki, podglad obu druzyn.
#include "vision/calibration.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace predeye;

TEST_CASE("Calibration: save/load round-trip (obie siatki)") {
    Calibration c;
    c.resolution = {1920, 1080};
    c.enemy_item_grid.origin = {1210, 300};
    c.enemy_item_grid.slot = {34, 34};
    c.enemy_item_grid.dx = 38;
    c.enemy_item_grid.dy = 96;
    c.enemy_item_grid.cols = 6;
    c.enemy_item_grid.rows = 5;
    c.ally_item_grid = c.enemy_item_grid;
    c.ally_item_grid.origin = {180, 310};

    const std::string path = (fs::temp_directory_path() / "predeye_calib_test.json").string();
    c.save(path);
    const Calibration l = Calibration::load(path);
    CHECK(l.resolution == c.resolution);
    CHECK(l.enemy_item_grid.origin == c.enemy_item_grid.origin);
    CHECK(l.enemy_item_grid.slot == c.enemy_item_grid.slot);
    CHECK(l.enemy_item_grid.dx == 38);
    CHECK(l.enemy_item_grid.dy == 96);
    CHECK(l.enemy_item_grid.cols == 6);
    CHECK(l.enemy_item_grid.rows == 5);
    CHECK(l.ally_item_grid.origin == cv::Point(180, 310));
    CHECK(l.ally_item_grid.slot == c.ally_item_grid.slot);
    CHECK(l.ally_item_grid.dx == 38);
}

TEST_CASE("Calibration: starszy plik bez ally_item_grid dostaje siatke lustrzana") {
    const std::string path = (fs::temp_directory_path() / "predeye_calib_old.json").string();
    {
        std::ofstream out(path, std::ios::trunc);
        out << R"({ "resolution": [1920, 1080],
                    "enemy_item_grid": { "origin": [1346, 256], "slot": [46, 46],
                                          "dx": 59, "dy": 145, "cols": 7, "rows": 5 } })";
    }
    const Calibration l = Calibration::load(path);
    // Geometria przejeta z wroga, origin.x odbity wzgledem srodka ekranu.
    CHECK(l.ally_item_grid.slot == l.enemy_item_grid.slot);
    CHECK(l.ally_item_grid.dx == l.enemy_item_grid.dx);
    CHECK(l.ally_item_grid.cols == l.enemy_item_grid.cols);
    const int span = 6 * 59 + 46;
    CHECK(l.ally_item_grid.origin.x == 1920 - 1346 - span);
    CHECK(l.ally_item_grid.origin.y == 256);
}

TEST_CASE("Calibration v2: round-trip siatek portretow i regionow draftu") {
    Calibration c = Calibration::default_for({1920, 1080});
    c.enemy_hero_grid.origin = {1280, 250};
    c.draft.enemy_bans = {{100, 40}, {50, 50}, 60, 0, 3, 1};
    c.draft.ally_picks = {{60, 200}, {70, 70}, 0, 90, 1, 5};

    const std::string path = (fs::temp_directory_path() / "predeye_calib_v2.json").string();
    c.save(path);
    const Calibration l = Calibration::load(path);
    CHECK(l.enemy_hero_grid.origin == cv::Point(1280, 250));
    CHECK(l.enemy_hero_grid.cols == 1);
    CHECK(l.ally_hero_grid.origin == c.ally_hero_grid.origin);
    REQUIRE(l.draft.present());
    CHECK(l.draft.enemy_bans.cols == 3);
    CHECK(l.draft.enemy_bans.rows == 1);
    CHECK(l.draft.ally_picks.rows == 5);
    // Regiony niezapisane pozostaja nieobecne.
    CHECK(!l.draft.ally_bans.present());
    CHECK(!l.draft.enemy_picks.present());
}

TEST_CASE("Calibration v2: starszy plik dostaje wyprowadzone siatki portretow, draft nieobecny") {
    const std::string path = (fs::temp_directory_path() / "predeye_calib_old2.json").string();
    {
        std::ofstream out(path, std::ios::trunc);
        out << R"({ "resolution": [1920, 1080],
                    "enemy_item_grid": { "origin": [1346, 256], "slot": [46, 46],
                                          "dx": 59, "dy": 145, "cols": 7, "rows": 5 } })";
    }
    const Calibration l = Calibration::load(path);
    // Kolumna portretow: 1 kolumna, tyle samo wierszy co itemy, na lewo od nich.
    CHECK(l.enemy_hero_grid.cols == 1);
    CHECK(l.enemy_hero_grid.rows == 5);
    CHECK(l.enemy_hero_grid.origin.x < l.enemy_item_grid.origin.x);
    // Sojusznicy: na prawo od swojej siatki itemow.
    const int ally_right = l.ally_item_grid.origin.x + 6 * l.ally_item_grid.dx +
                           l.ally_item_grid.slot.width;
    CHECK(l.ally_hero_grid.origin.x >= ally_right);
    CHECK(!l.draft.present());
}

TEST_CASE("Calibration: czytelne bledy dla zepsutego pliku") {
    const std::string path = (fs::temp_directory_path() / "predeye_calib_bad.json").string();
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{ \"resolution\": [1920, 1080] }";
    }
    CHECK_THROWS_WITH_AS(Calibration::load(path),
                         doctest::Contains("enemy_item_grid"), std::runtime_error);
    CHECK_THROWS_AS(Calibration::load("/nie/ma/takiego/pliku.json"), std::runtime_error);
}

TEST_CASE("GridSpec: geometria slotow") {
    GridSpec g;
    g.origin = {100, 200};
    g.slot = {30, 32};
    g.dx = 40;
    g.dy = 90;
    const cv::Rect r00 = g.slot_rect(0, 0);
    CHECK(r00 == cv::Rect(100, 200, 30, 32));
    CHECK(g.slot_rect(0, 3) == cv::Rect(220, 200, 30, 32));
    CHECK(g.slot_rect(2, 1) == cv::Rect(140, 380, 30, 32));
}

TEST_CASE("mirror_grid: odbicie originu wzgledem srodka, geometria bez zmian") {
    GridSpec e;
    e.origin = {1346, 256};
    e.slot = {46, 46};
    e.dx = 59;
    e.dy = 145;
    e.cols = 7;
    e.rows = 5;
    const GridSpec a = mirror_grid(e, {1920, 1080});
    CHECK(a.origin.x == 1920 - 1346 - (6 * 59 + 46));
    CHECK(a.origin.y == 256);
    CHECK(a.slot == e.slot);
    CHECK(a.cols == e.cols);
}

TEST_CASE("draw_grid: nie modyfikuje oryginalu, rysuje obie siatki") {
    const Calibration c = Calibration::default_for({1920, 1080});
    cv::Mat frame(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
    const cv::Mat preview = draw_grid(frame, c);
    CHECK(preview.size() == frame.size());
    CHECK(cv::sum(frame) == cv::Scalar(0, 0, 0, 0)); // oryginal nietkniety
    // Czerwona ramka pierwszego slotu wroga i zielona sojusznika narysowane.
    const cv::Rect re = c.enemy_item_grid.slot_rect(0, 0);
    CHECK(preview.at<cv::Vec3b>(re.y, re.x + re.width / 2) == cv::Vec3b(0, 0, 255));
    const cv::Rect ra = c.ally_item_grid.slot_rect(0, 0);
    CHECK(preview.at<cv::Vec3b>(ra.y, ra.x + ra.width / 2) == cv::Vec3b(0, 255, 0));
}
