// vision/vision_session — implementacja fasady toru live (patrz .hpp).
#include "vision/vision_session.hpp"

#include "vision/scoreboard_reader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace predeye {

VisionSession::VisionSession(std::string cache_dir)
    : icon_dir_(cache_dir + "/icons"), api_(std::move(cache_dir)) {}

void VisionSession::ensure_loaded() {
    if (loaded_)
        return;
    heroes_ = std::make_unique<HeroDB>(api_.heroes());
    items_ = parse_items(api_.items());
    index_ = index_by_id(items_);
    engine_ = std::make_unique<BuildEngine>(items_);
    loaded_ = true;
}

void VisionSession::ensure_matcher() {
    ensure_loaded();
    if (matcher_)
        return;
    // Konstruktor pobiera brakujace ikony; po pierwszym pobraniu dziala offline.
    matcher_ = std::make_unique<IconMatcher>(items_, api_, icon_dir_);
}

std::vector<std::string> VisionSession::hero_names() {
    ensure_loaded();
    std::vector<std::string> names;
    names.reserve(heroes_->all().size());
    for (const auto& h : heroes_->all())
        names.push_back(h.name);
    std::sort(names.begin(), names.end());
    return names;
}

std::string VisionSession::set_objective(const std::string& hero, Role role, int budget_override) {
    ensure_loaded();
    const auto me = heroes_->by_names({hero}).front(); // rzuca gdy nieznany
    obj_ = objective_for(me, role, budget_override);
    has_obj_ = true;
    reset_diff(); // zmiana celu = nowa sesja diffa
    return me.name;
}

LiveResult VisionSession::read(const cv::Mat& frame, const Calibration& calib) {
    ensure_matcher();
    if (!has_obj_)
        throw std::runtime_error("VisionSession::read: brak celu — wywolaj set_objective");
    if (matcher_->base_size() == 0)
        throw std::runtime_error("pusta baza ikon — uruchom fetch-icons / ensure_matcher");

    const ScoreboardRead sb = read_scoreboard(frame, calib, *matcher_, index_);

    LiveResult out;
    out.total_items = sb.total_items;
    out.uncertain = sb.uncertain;
    out.objective_name = obj_.name;

    std::vector<EnemyBuildProfile> builds;
    std::vector<std::set<std::string>> next_prev;
    next_prev.reserve(sb.enemies.size());

    for (const auto& e : sb.enemies) {
        LiveEnemyView view;
        view.row = e.row;
        std::set<std::string> names;

        for (const auto& s : e.slots) {
            LiveSlotView sv;
            sv.empty = s.empty;
            if (!s.empty) {
                sv.name = s.name.empty() ? "?" : s.name;
                sv.confident = s.confident;
                if (!s.name.empty())
                    names.insert(s.name);
            }
            view.slots.push_back(std::move(sv));
        }

        // Diff wzgledem poprzedniego odczytu tego wiersza (dodane, potem usuniete).
        if (e.row < static_cast<int>(prev_.size())) {
            const auto& before = prev_[static_cast<size_t>(e.row)];
            for (const auto& n : names)
                if (!before.count(n))
                    view.changes.push_back("+" + n);
            for (const auto& n : before)
                if (!names.count(n))
                    view.changes.push_back("-" + n);
        }

        if (!e.items.empty())
            builds.push_back(classify_items(e.items));
        next_prev.push_back(std::move(names));
        out.enemies.push_back(std::move(view));
    }

    refine_enemy_profile(out.profile, builds);
    out.counter = engine_->counter_build(obj_, out.profile);
    prev_ = std::move(next_prev);
    return out;
}

} // namespace predeye
