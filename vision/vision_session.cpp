// vision/vision_session — implementacja fasady toru live (patrz .hpp).
#include "vision/vision_session.hpp"

#include "vision/scoreboard_reader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace predeye {
namespace {

// Widoki wierszy jednej druzyny + diff wzgledem poprzedniego odczytu.
// `prev` jest podmieniane na biezacy stan (do nastepnego diffa).
// `heroes` (po wierszach, moze byc krotsze) dopisuje bohatera z portretu.
std::vector<LiveRowView> make_views(const std::vector<RowRead>& rows,
                                    const std::vector<HeroRead>& heroes,
                                    std::vector<std::set<std::string>>& prev) {
    std::vector<LiveRowView> views;
    std::vector<std::set<std::string>> next;
    next.reserve(rows.size());

    for (const auto& r : rows) {
        LiveRowView view;
        view.row = r.row;
        view.role = scoreboard_row_role(r.row, static_cast<int>(rows.size()));
        view.role_label = view.role == Role::Unknown ? "Wiersz " + std::to_string(r.row + 1)
                                                     : role_name(view.role);
        if (r.row >= 0 && r.row < static_cast<int>(heroes.size())) {
            view.hero = heroes[static_cast<size_t>(r.row)].name;
            view.hero_confident = heroes[static_cast<size_t>(r.row)].confident;
        }
        std::set<std::string> names;

        for (const auto& s : r.slots) {
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
        if (r.row < static_cast<int>(prev.size())) {
            const auto& before = prev[static_cast<size_t>(r.row)];
            for (const auto& n : names)
                if (!before.count(n))
                    view.changes.push_back("+" + n);
            for (const auto& n : before)
                if (!names.count(n))
                    view.changes.push_back("-" + n);
        }

        next.push_back(std::move(names));
        views.push_back(std::move(view));
    }
    prev = std::move(next);
    return views;
}

} // namespace

VisionSession::VisionSession(std::string cache_dir)
    : icon_dir_(cache_dir + "/icons"), portrait_dir_(cache_dir + "/portraits"),
      api_(std::move(cache_dir)) {}

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
    // Konstruktory pobieraja brakujace grafiki; po pierwszym pobraniu offline.
    matcher_ = std::make_unique<IconMatcher>(items_, api_, icon_dir_);
    hero_matcher_ = std::make_unique<HeroMatcher>(heroes_->all(), api_, portrait_dir_);
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
    const auto enemy_heroes =
        read_heroes(frame, calib.enemy_item_grid, calib.resolution, *hero_matcher_, *heroes_);
    const auto ally_heroes =
        read_heroes(frame, calib.ally_item_grid, calib.resolution, *hero_matcher_, *heroes_);

    LiveResult out;
    out.total_items = sb.total_items;
    out.uncertain = sb.uncertain;
    out.objective_name = obj_.name;
    out.enemies = make_views(sb.enemies, enemy_heroes, prev_enemy_);
    out.allies = make_views(sb.allies, ally_heroes, prev_ally_);

    // Profil wroga: baza z KLAS rozpoznanych bohaterow (jak `counter` z nazw
    // wpisanych recznie), doostrzona REALNYMI itemami ze scoreboardu.
    // Sojusznicy wyswietlani informacyjnie (kto z kim walczy, co juz maja).
    std::vector<HeroProfile> enemy_profiles;
    for (const auto& h : enemy_heroes)
        if (h.confident)
            if (const HeroProfile* p = heroes_->by_id(h.hero_id))
                enemy_profiles.push_back(*p);
    if (!enemy_profiles.empty())
        out.profile = enemy_from(enemy_profiles);

    std::vector<EnemyBuildProfile> builds;
    for (const auto& e : sb.enemies)
        if (!e.items.empty())
            builds.push_back(classify_items(e.items));

    refine_enemy_profile(out.profile, builds);
    out.counter = engine_->counter_build(obj_, out.profile);
    return out;
}

} // namespace predeye
