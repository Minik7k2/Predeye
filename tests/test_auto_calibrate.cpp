// Testy auto_calibrate — syntetyczny scoreboard z przesunieta siatka:
// auto-kalibracja ma odzyskac przesuniecie originu (bez sieci, §9).
#include "vision/auto_calibrate.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <random>

namespace fs = std::filesystem;
using namespace predeye;

namespace {

cv::Mat synth_icon(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    cv::Mat icon(64, 64, CV_8UC3);
    for (int by = 0; by < 8; ++by)
        for (int bx = 0; bx < 8; ++bx)
            cv::rectangle(icon, {bx * 8, by * 8, 8, 8}, cv::Scalar(d(rng), d(rng), d(rng)),
                          cv::FILLED);
    return icon;
}

std::string make_icon_dir() {
    const fs::path dir = fs::temp_directory_path() / "predeye_test_ac_icons";
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

void paste_icon(cv::Mat& frame, const cv::Rect& r, uint32_t seed) {
    cv::Mat resized;
    cv::resize(synth_icon(seed), resized, r.size(), 0, 0, cv::INTER_AREA);
    resized.copyTo(frame(r));
}

} // namespace

TEST_CASE("auto_calibrate: odzyskuje przesuniecie siatki wzgledem punktu startu") {
    const IconMatcher matcher(make_icon_dir());
    REQUIRE(matcher.base_size() == 5);

    // Baza: mala siatka w malej klatce; realna siatka przesunieta o (17, -11).
    Calibration base;
    base.resolution = {400, 300};
    base.enemy_item_grid = {{60, 60}, {40, 40}, 50, 60, 3, 2};
    base.ally_item_grid = {{5000, 5000}, {40, 40}, 50, 60, 3, 2}; // poza klatka
    base.enemy_hero_grid = hero_grid_for(base.enemy_item_grid, true);
    base.ally_hero_grid = hero_grid_for(base.ally_item_grid, false);

    GridSpec truth = base.enemy_item_grid;
    truth.origin = {60 + 17, 60 - 11};

    cv::Mat frame(300, 400, CV_8UC3, cv::Scalar(10, 10, 12));
    paste_icon(frame, truth.slot_rect(0, 0), 1);
    paste_icon(frame, truth.slot_rect(1, 0), 2);
    paste_icon(frame, truth.slot_rect(0, 1), 3);

    const AutoCalibResult res = auto_calibrate(frame, matcher, base);
    CHECK(res.ok());
    CHECK(res.enemy_confidence > 0.9f);
    // Odzyskany origin w granicach 2 px (dokladnosc dopasowania NCC).
    CHECK(std::abs(res.calib.enemy_item_grid.origin.x - truth.origin.x) <= 2);
    CHECK(std::abs(res.calib.enemy_item_grid.origin.y - truth.origin.y) <= 2);
    // Siatka portretow przesunieta razem z siatka itemow.
    CHECK(res.calib.enemy_hero_grid.origin.x - base.enemy_hero_grid.origin.x ==
          res.calib.enemy_item_grid.origin.x - base.enemy_item_grid.origin.x);
    // Sojusznicy poza klatka: brak kotwicy -> pewnosc ujemna, siatka bez zmian.
    CHECK(res.ally_confidence < 0.0f);
    CHECK(res.calib.ally_item_grid.origin == base.ally_item_grid.origin);
}

TEST_CASE("auto_calibrate: pusta klatka -> niska pewnosc, nie rzuca") {
    const IconMatcher matcher(make_icon_dir());
    Calibration base;
    base.resolution = {400, 300};
    base.enemy_item_grid = {{60, 60}, {40, 40}, 50, 60, 3, 2};
    base.ally_item_grid = base.enemy_item_grid;

    cv::Mat frame(300, 400, CV_8UC3, cv::Scalar(10, 10, 12)); // same puste sloty
    AutoCalibResult res;
    REQUIRE_NOTHROW(res = auto_calibrate(frame, matcher, base));
    CHECK(!res.ok());
}
