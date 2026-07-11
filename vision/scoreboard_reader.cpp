#include "scoreboard_reader.hpp"

namespace predeye {

ScoreboardRead read_scoreboard(const cv::Mat& frame_bgr, const Calibration& calib,
                               const IconMatcher& matcher, const ItemIndex& index) {
    ScoreboardRead out;
    const GridSpec& g = calib.enemy_item_grid;
    const cv::Rect frame_rect(0, 0, frame_bgr.cols, frame_bgr.rows);

    for (int r = 0; r < g.rows; ++r) {
        EnemyRead row;
        row.row = r;
        for (int c = 0; c < g.cols; ++c) {
            SlotRead slot;
            slot.row = r;
            slot.col = c;

            const cv::Rect rect = g.slot_rect(r, c);
            // ROI musi w calosci miescic sie w klatce; zla kalibracja lub
            // nadmiarowe kolumny (spec 6, realne UI 7) daja slot pusty.
            if ((rect & frame_rect) != rect || rect.area() == 0) {
                row.slots.push_back(slot);
                continue;
            }

            const cv::Mat roi = frame_bgr(rect);
            if (IconMatcher::looks_empty(roi)) {
                row.slots.push_back(slot);
                continue;
            }

            const MatchResult m = matcher.match(roi);
            slot.empty = false;
            slot.item_id = m.item_id;
            slot.cosine = m.cosine;
            slot.confident = m.confident();
            slot.top3 = m.top3;

            const auto it = index.find(m.item_id);
            if (it != index.end()) {
                slot.name = it->second.display_name;
                row.items.push_back(it->second);
            }
            if (!slot.confident)
                ++row.uncertain;
            ++out.total_items;
            row.slots.push_back(slot);
        }
        out.uncertain += row.uncertain;
        out.enemies.push_back(std::move(row));
    }
    return out;
}

} // namespace predeye
