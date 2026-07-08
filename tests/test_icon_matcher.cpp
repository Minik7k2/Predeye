// Testy matchera NCC — offline, na syntetycznych ikonach (bez sieci, §9).
#include "vision/icon_matcher.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <random>

namespace fs = std::filesystem;
using namespace predeye;

namespace {

// Deterministyczne "ikony" — kolorowe wzory o wyraznej strukturze.
cv::Mat synth_icon(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    cv::Mat icon(64, 64, CV_8UC3);
    for (int by = 0; by < 8; ++by)
        for (int bx = 0; bx < 8; ++bx) {
            const cv::Scalar color(d(rng), d(rng), d(rng));
            cv::rectangle(icon, {bx * 8, by * 8, 8, 8}, color, cv::FILLED);
        }
    return icon;
}

std::string make_icon_dir() {
    const fs::path dir = fs::temp_directory_path() / "predeye_test_icons";
    fs::create_directories(dir);
    std::string manifest = "{";
    for (long long id = 1; id <= 5; ++id) {
        const std::string file = std::to_string(id) + ".png";
        cv::imwrite((dir / file).string(), synth_icon(static_cast<uint32_t>(id)));
        manifest += (id > 1 ? "," : "");
        manifest += "\"" + std::to_string(id) + "\":\"" + file + "\"";
    }
    manifest += "}";
    std::ofstream out(dir / "manifest.json", std::ios::trunc);
    out << manifest;
    return dir.string();
}

} // namespace

TEST_CASE("ncc_signature: srednia 0, norma L2 = 1, wymiar 3072") {
    const cv::Mat sig = ncc_signature(synth_icon(7));
    CHECK(sig.rows == 1);
    CHECK(sig.cols == 32 * 32 * 3);
    CHECK(cv::mean(sig)[0] == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(cv::norm(sig) == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("IconMatcher offline: rozpoznaje znieksztalcone ikony") {
    const IconMatcher matcher(make_icon_dir());
    REQUIRE(matcher.base_size() == 5);

    std::mt19937 rng(123);
    for (long long id = 1; id <= 5; ++id) {
        // Symulacja capture: mniejsza skala + jasnosc + lekki szum.
        cv::Mat probe;
        cv::resize(synth_icon(static_cast<uint32_t>(id)), probe, {40, 40}, 0, 0,
                   cv::INTER_AREA);
        probe.convertTo(probe, CV_8UC3, 1.05, 8);
        cv::Mat noise(probe.size(), CV_32FC3);
        cv::randn(noise, 0.0, 6.0);
        cv::Mat f;
        probe.convertTo(f, CV_32FC3);
        f += noise;
        f.convertTo(probe, CV_8UC3);

        const MatchResult res = matcher.match(probe);
        CHECK(res.item_id == id);
        CHECK(res.cosine > 0.8f);
        CHECK(res.confident());
        CHECK(res.top3[0].first == id);
        CHECK(res.top3[0].second >= res.top3[1].second);
        CHECK(res.top3[1].second >= res.top3[2].second);
    }
}

TEST_CASE("looks_empty: ciemny jednolity slot vs ikona") {
    const cv::Mat dark(48, 48, CV_8UC3, cv::Scalar(12, 10, 14));
    CHECK(IconMatcher::looks_empty(dark));
    CHECK_FALSE(IconMatcher::looks_empty(synth_icon(3)));
}
