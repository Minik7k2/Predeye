// Testy draft_reader — offline, syntetyczny ekran draftu (bez sieci, §9).
// Portrety bohaterow symulowane synth-ikonami (jak w tescie scoreboardu).
#include "vision/draft_reader.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
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

// Baza "portretow" bohaterow o id 101..105 z jawnego manifestu.
IconMatcher make_hero_matcher() {
    const fs::path dir = fs::temp_directory_path() / "predeye_test_draft_icons";
    fs::create_directories(dir);
    std::map<long long, std::string> manifest;
    for (long long id = 101; id <= 105; ++id) {
        const std::string file = std::to_string(id) + ".png";
        cv::imwrite((dir / file).string(), synth_icon(static_cast<uint32_t>(id)));
        manifest[id] = file;
    }
    return IconMatcher(manifest, dir.string());
}

// HeroDB z bohaterami o id 101..105 (Hero101...).
HeroDB make_heroes() {
    nlohmann::json j = nlohmann::json::array();
    for (long long id = 101; id <= 105; ++id) {
        j.push_back({{"id", id},
                     {"display_name", "Hero" + std::to_string(id)},
                     {"slug", "hero-" + std::to_string(id)},
                     {"classes", {"Mage"}},
                     {"roles", {"Midlane"}}});
    }
    return HeroDB(j);
}

void paste_icon(cv::Mat& frame, const cv::Rect& r, uint32_t seed) {
    cv::Mat resized;
    cv::resize(synth_icon(seed), resized, r.size(), 0, 0, cv::INTER_AREA);
    resized.copyTo(frame(r));
}

Calibration make_draft_calib() {
    Calibration c;
    c.resolution = {320, 240};
    // Bany: 1 wiersz x 2 sloty na gorze; picki: kolumna 2 portretow.
    c.draft.enemy_bans = {{10, 10}, {40, 40}, 50, 0, 2, 1};
    c.draft.ally_picks = {{10, 70}, {40, 40}, 0, 50, 1, 2};
    // Pozostale regiony nieobecne.
    c.draft.ally_bans.rows = 0;
    c.draft.enemy_picks.rows = 0;
    return c;
}

} // namespace

TEST_CASE("read_draft: rozpoznaje bany i picki, puste sloty zostaja puste") {
    const IconMatcher matcher = make_hero_matcher();
    REQUIRE(matcher.base_size() == 5);
    const HeroDB heroes = make_heroes();
    const Calibration calib = make_draft_calib();

    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(10, 10, 12));
    paste_icon(frame, calib.draft.enemy_bans.slot_rect(0, 0), 101); // ban: Hero101
    // Drugi slot banu pusty (jeszcze nie zbanowano).
    paste_icon(frame, calib.draft.ally_picks.slot_rect(0, 0), 103); // pick: Hero103
    paste_icon(frame, calib.draft.ally_picks.slot_rect(1, 0), 105); // pick: Hero105

    const DraftRead read = read_draft(frame, calib, matcher, heroes);
    CHECK(read.calibrated);

    REQUIRE(read.enemy_bans.size() == 2);
    CHECK(!read.enemy_bans[0].empty);
    CHECK(read.enemy_bans[0].hero_id == 101);
    CHECK(read.enemy_bans[0].hero_name == "Hero101");
    CHECK(read.enemy_bans[0].confident);
    CHECK(read.enemy_bans[1].empty);

    REQUIRE(read.ally_picks.size() == 2);
    CHECK(read.ally_picks[0].hero_name == "Hero103");
    CHECK(read.ally_picks[1].hero_name == "Hero105");

    // Regiony nieobecne -> puste listy.
    CHECK(read.ally_bans.empty());
    CHECK(read.enemy_picks.empty());

    // names() zbiera tylko pewne, niepuste sloty.
    const auto picks = DraftRead::names(read.ally_picks);
    REQUIRE(picks.size() == 2);
    CHECK(picks[0] == "Hero103");
}

TEST_CASE("read_draft: brak kalibracji draftu -> calibrated=false, bez odczytu") {
    const IconMatcher matcher = make_hero_matcher();
    const HeroDB heroes = make_heroes();
    Calibration calib; // draft domyslnie obecny? nie — zerujemy jawnie
    calib.draft.ally_bans.rows = calib.draft.enemy_bans.rows = 0;
    calib.draft.ally_picks.rows = calib.draft.enemy_picks.rows = 0;

    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(10, 10, 12));
    const DraftRead read = read_draft(frame, calib, matcher, heroes);
    CHECK(!read.calibrated);
    CHECK(read.ally_bans.empty());
    CHECK(read.enemy_picks.empty());
}
