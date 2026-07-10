// Testy kalibracji: round-trip JSON, geometria siatki, podglad.
#include "vision/calibration.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace predeye;

TEST_CASE("Calibration: save/load round-trip") {
    Calibration c;
    c.resolution = {1920, 1080};
    c.enemy_item_grid.origin = {1210, 300};
    c.enemy_item_grid.slot = {34, 34};
    c.enemy_item_grid.dx = 38;
    c.enemy_item_grid.dy = 96;
    c.enemy_item_grid.cols = 6;
    c.enemy_item_grid.rows = 5;

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

TEST_CASE("draw_grid: nie modyfikuje oryginalu, rysuje w obrysie slotow") {
    const Calibration c = Calibration::default_for({1920, 1080});
    cv::Mat frame(1080, 1920, CV_8UC3, cv::Scalar(0, 0, 0));
    const cv::Mat preview = draw_grid(frame, c);
    CHECK(preview.size() == frame.size());
    CHECK(cv::sum(frame) == cv::Scalar(0, 0, 0, 0)); // oryginal nietkniety
    // Zielona ramka pierwszego slotu faktycznie narysowana.
    const cv::Rect r = c.enemy_item_grid.slot_rect(0, 0);
    const cv::Vec3b px = preview.at<cv::Vec3b>(r.y, r.x + r.width / 2);
    CHECK(px == cv::Vec3b(0, 255, 0));
}
