// Testy auto-kalibracji: syntetyczny scoreboard (mechanika detekcji) oraz
// realne zrzuty z tests/fixtures/screens (zgodnosc ze zmierzona kalibracja).
#include "vision/auto_calibration.hpp"

#include <cmath>
#include <doctest/doctest.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace predeye;

namespace {

// Panel jak na realnym scoreboardzie: sloty 52 px wnetrza w ramkach 2 px,
// przerwy 4 px (krok 60), crest 18 px przed item1, dy 145. Czesc slotow
// "wypelniona" jasna ikona — wiersze pelne daja linie wzoru takze w srodku.
void draw_panel(cv::Mat& img, int origin_x, int origin_y, int rows, int cols, int filled) {
    const int dx = 60, dy = 145, inner = 52;
    for (int r = 0; r < rows; ++r) {
        for (int c = -1; c < cols; ++c) { // c == -1: crest poza siatka
            int x = origin_x + c * dx;
            if (c < 0)
                x = origin_x - 18 - inner - 2; // przerwa za crestem > gap_max
            const int y = origin_y + r * dy;
            cv::rectangle(img, {x - 2, y - 2, inner + 4, inner + 4}, cv::Scalar(90, 90, 90), 2);
            if (c >= 0 && c < filled)
                cv::rectangle(img, {x, y, inner, inner}, cv::Scalar(170, 160, 150), cv::FILLED);
            else
                cv::rectangle(img, {x, y, inner, inner}, cv::Scalar(22, 22, 22), cv::FILLED);
        }
    }
}

} // namespace

TEST_CASE("auto_calibrate: syntetyczny scoreboard — oba panele, crest poza siatka") {
    cv::Mat img(1080, 1920, CV_8UC3, cv::Scalar(15, 15, 15));
    draw_panel(img, 333, 250, 5, 6, 4);  // sojusznicy: 4 itemy w wierszu
    draw_panel(img, 1343, 250, 5, 6, 0); // wrogowie: pusto (same ramki)

    const auto res = auto_calibrate(img);
    REQUIRE(res.has_value());
    const GridSpec& e = res->calib.enemy_item_grid;
    const GridSpec& a = res->calib.ally_item_grid;
    CHECK(e.cols == 6);
    CHECK(e.rows == 5);
    CHECK(e.dx == 60);
    CHECK(e.dy == 145);
    // Ramki syntetyczne maja ostre rogi — origin/slot moga siegac ramki (2 px).
    CHECK(std::abs(e.origin.x - 1343) <= 2);
    CHECK(std::abs(e.origin.y - 250) <= 2);
    CHECK(std::abs(e.slot.width - 52) <= 4);
    CHECK(std::abs(a.origin.x - 333) <= 2);
    CHECK(a.cols == 6);
    CHECK(res->rows_ally == 5);
    CHECK(res->rows_enemy == 5);
}

TEST_CASE("auto_calibrate: brak siatki na pustym obrazie") {
    cv::Mat img(1080, 1920, CV_8UC3, cv::Scalar(40, 40, 40));
    CHECK_FALSE(auto_calibrate(img).has_value());
}

TEST_CASE("auto_calibrate: realne zrzuty 1080p zgodne ze zmierzona kalibracja") {
    const Calibration expected =
        Calibration::load(PREDEYE_FIXTURES_DIR "/screens/calibration_1080p.json");
    for (const char* name : {"/screens/scoreboard_1080p_01.png", "/screens/match1_01.png",
                             "/screens/match2_01.png"}) {
        CAPTURE(name);
        const cv::Mat frame = cv::imread(std::string(PREDEYE_FIXTURES_DIR) + name);
        REQUIRE(!frame.empty());
        const auto res = auto_calibrate(frame);
        REQUIRE(res.has_value());
        const GridSpec& e = res->calib.enemy_item_grid;
        const GridSpec& a = res->calib.ally_item_grid;
        CHECK(e.cols == expected.enemy_item_grid.cols);
        CHECK(e.rows == 5);
        CHECK(e.dx == expected.enemy_item_grid.dx);
        CHECK(e.dy == expected.enemy_item_grid.dy);
        CHECK(std::abs(e.origin.x - expected.enemy_item_grid.origin.x) <= 2);
        CHECK(std::abs(e.origin.y - expected.enemy_item_grid.origin.y) <= 2);
        CHECK(std::abs(e.slot.width - expected.enemy_item_grid.slot.width) <= 2);
        CHECK(std::abs(a.origin.x - expected.ally_item_grid.origin.x) <= 2);
        CHECK(std::abs(a.origin.y - expected.ally_item_grid.origin.y) <= 2);
    }
}
