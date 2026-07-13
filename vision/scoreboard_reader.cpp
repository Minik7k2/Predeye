#include "scoreboard_reader.hpp"

namespace predeye {
namespace {

// Odczyt jednej siatki (druzyny): rows x cols slotow.
std::vector<RowRead> read_grid(const cv::Mat& frame_bgr, const GridSpec& g,
                               const IconMatcher& matcher, const ItemIndex& index,
                               int& total_items, int& uncertain) {
    std::vector<RowRead> out;
    const cv::Rect frame_rect(0, 0, frame_bgr.cols, frame_bgr.rows);

    for (int r = 0; r < g.rows; ++r) {
        RowRead row;
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
            ++total_items;
            row.slots.push_back(slot);
        }
        uncertain += row.uncertain;
        out.push_back(std::move(row));
    }
    return out;
}

} // namespace

Role scoreboard_row_role(int row, int rows) {
    // Staly porzadek rol w scoreboardzie Predecessora (ZALOZENIE — patrz .hpp).
    static constexpr Role kOrder[5] = {Role::Offlane, Role::Jungle, Role::Midlane, Role::Carry,
                                       Role::Support};
    if (rows != 5 || row < 0 || row >= 5)
        return Role::Unknown;
    return kOrder[row];
}

ScoreboardRead read_scoreboard(const cv::Mat& frame_bgr, const Calibration& calib,
                               const IconMatcher& matcher, const ItemIndex& index) {
    ScoreboardRead out;
    out.enemies =
        read_grid(frame_bgr, calib.enemy_item_grid, matcher, index, out.total_items, out.uncertain);
    out.allies =
        read_grid(frame_bgr, calib.ally_item_grid, matcher, index, out.total_items, out.uncertain);
    return out;
}

std::vector<HeroRead> read_heroes(const cv::Mat& frame_bgr, const GridSpec& grid,
                                  const cv::Size& resolution, const HeroMatcher& matcher,
                                  const HeroDB& db) {
    std::vector<HeroRead> out;
    const cv::Rect frame_rect(0, 0, frame_bgr.cols, frame_bgr.rows);
    for (int r = 0; r < grid.rows; ++r) {
        HeroRead h;
        h.row = r;
        const cv::Rect roi = portrait_rect(grid, r, resolution);
        // Portret uciety przez krawedz klatki bylby znieksztalcona probka —
        // wymagamy pelnego ROI (zla kalibracja => pomijamy, nie rzucamy).
        if ((roi & frame_rect) == roi && matcher.base_size() > 0) {
            const MatchResult m = matcher.match(frame_bgr(roi));
            h.hero_id = m.item_id;
            h.cosine = m.cosine;
            h.confident = m.confident();
            if (const HeroProfile* p = db.by_id(h.hero_id))
                h.name = p->name;
        }
        out.push_back(std::move(h));
    }
    return out;
}

} // namespace predeye
