// Testy vision/capture: zapis klatki odczytu (save_capture_png) — reszta
// (DXGI) wymaga zywej gry i nie nadaje sie do testu automatycznego.
#include "vision/capture.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>

using namespace predeye;
namespace fs = std::filesystem;

TEST_CASE("save_capture_png: zapisuje PNG i zwraca sciezke") {
    const cv::Mat frame(60, 80, CV_8UC3, cv::Scalar(10, 20, 30));
    const fs::path dir = fs::temp_directory_path() / "predeye_captures_test";
    const std::string path = save_capture_png(frame, dir.string());
    REQUIRE(!path.empty());
    CHECK(fs::exists(fs::path(path)));
    const cv::Mat back = cv::imread(path);
    CHECK(back.cols == frame.cols);
    CHECK(back.rows == frame.rows);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("save_capture_png: pusta klatka i zly katalog nie rzucaja") {
    CHECK(save_capture_png(cv::Mat{}) == "");
    const cv::Mat frame(8, 8, CV_8UC3, cv::Scalar(0, 0, 0));
    // Niedozwolona nazwa katalogu na Windows; na innych systemach zapis moze
    // sie udac — sprawdzamy tylko brak wyjatku.
    CHECK_NOTHROW(save_capture_png(frame, "con:/\"bad\""));
}
