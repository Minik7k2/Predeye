#include "draft_reader.hpp"

namespace predeye {
namespace {

// Odczyt jednej siatki draftu: kazdy slot to portret bohatera albo pusty
// (jeszcze nie zbanowano/wybrano). Puste sloty draftu sa ciemne/jednolite —
// looks_empty dziala jak dla itemow; dodatkowo odsiewamy progiem pewnosci.
std::vector<DraftSlot> read_draft_grid(const cv::Mat& frame_bgr, const GridSpec& g,
                                       const IconMatcher& hero_matcher, const HeroDB& heroes) {
    std::vector<DraftSlot> out;
    if (!g.present())
        return out;
    const cv::Rect frame_rect(0, 0, frame_bgr.cols, frame_bgr.rows);

    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            DraftSlot slot;
            slot.index = r * g.cols + c;
            const cv::Rect rect = g.slot_rect(r, c);
            if ((rect & frame_rect) != rect || rect.area() == 0) {
                out.push_back(slot);
                continue;
            }
            const cv::Mat roi = frame_bgr(rect);
            if (IconMatcher::looks_empty(roi)) {
                out.push_back(slot);
                continue;
            }
            const MatchResult m = hero_matcher.match(roi);
            slot.empty = false;
            slot.hero_id = m.item_id;
            slot.cosine = m.cosine;
            slot.confident = m.confident();
            if (const HeroProfile* h = heroes.by_id(m.item_id))
                slot.hero_name = h->name;
            out.push_back(slot);
        }
    }
    return out;
}

} // namespace

std::vector<std::string> DraftRead::names(const std::vector<DraftSlot>& slots) {
    std::vector<std::string> out;
    for (const auto& s : slots)
        if (!s.empty && s.confident && !s.hero_name.empty())
            out.push_back(s.hero_name);
    return out;
}

DraftRead read_draft(const cv::Mat& frame_bgr, const Calibration& calib,
                     const IconMatcher& hero_matcher, const HeroDB& heroes) {
    DraftRead out;
    out.calibrated = calib.draft.present();
    if (!out.calibrated || hero_matcher.base_size() == 0)
        return out;
    out.ally_bans = read_draft_grid(frame_bgr, calib.draft.ally_bans, hero_matcher, heroes);
    out.enemy_bans = read_draft_grid(frame_bgr, calib.draft.enemy_bans, hero_matcher, heroes);
    out.ally_picks = read_draft_grid(frame_bgr, calib.draft.ally_picks, hero_matcher, heroes);
    out.enemy_picks = read_draft_grid(frame_bgr, calib.draft.enemy_picks, hero_matcher, heroes);
    return out;
}

} // namespace predeye
