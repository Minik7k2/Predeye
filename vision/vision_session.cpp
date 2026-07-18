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
// `prev` jest podmieniane na biezacy stan (do nastepnego diffa). Tozsamosc
// bohatera (z portretu) przychodzi w RowRead.
std::vector<LiveRowView> make_views(const std::vector<RowRead>& rows,
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
        view.hero_name = r.hero_confident ? r.hero_name : "";
        view.hero_confident = r.hero_confident;
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

// Tie-break niepewnych slotow po typowym buildzie bohatera: jesli top-1 nie
// nalezy do typowego buildu, a ktorys z top-3 tak — wybierz kandydata z
// buildu. Slot pozostaje "niepewny" (cosine ponizej progu), zmienia sie tylko
// najlepszy strzal. Przebudowuje row.items.
void apply_typical_tiebreak(RowRead& row, const std::set<long long>& typical,
                            const ItemIndex& index) {
    bool changed = false;
    for (auto& s : row.slots) {
        if (s.empty || s.confident || typical.count(s.item_id))
            continue;
        for (size_t k = 1; k < s.top3.size(); ++k) {
            const auto [cand_id, cand_cos] = s.top3[k];
            if (cand_id == 0 || !typical.count(cand_id))
                continue;
            s.item_id = cand_id;
            s.cosine = cand_cos;
            const auto it = index.find(cand_id);
            s.name = it != index.end() ? it->second.display_name : "";
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    row.items.clear();
    for (const auto& s : row.slots) {
        if (s.empty)
            continue;
        const auto it = index.find(s.item_id);
        if (it != index.end())
            row.items.push_back(it->second);
    }
}

} // namespace

VisionSession::VisionSession(std::string cache_dir)
    : icon_dir_(cache_dir + "/icons"), hero_icon_dir_(cache_dir + "/hero_icons"),
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
    // Konstruktor pobiera brakujace grafiki; po pierwszym pobraniu offline.
    matcher_ = std::make_unique<IconMatcher>(items_, api_, icon_dir_);
}

void VisionSession::ensure_hero_matcher() {
    ensure_loaded();
    if (hero_matcher_)
        return;
    const auto manifest = ensure_hero_portrait_cache(heroes_->all(), api_, hero_icon_dir_);
    hero_matcher_ = std::make_unique<IconMatcher>(manifest, hero_icon_dir_);
}

const std::set<long long>* VisionSession::typical_items(long long hero_id,
                                                        const std::string& name) {
    auto it = typical_cache_.find(hero_id);
    if (it == typical_cache_.end()) {
        std::optional<std::set<long long>> entry;
        try {
            if (auto build = typical_build(api_, index_, hero_id, name)) {
                std::set<long long> ids;
                for (const auto& item : build->items)
                    ids.insert(item.id);
                entry = std::move(ids);
            }
        } catch (const std::exception&) {
            // Blad sieci nie moze wywalac odczytu live — tie-break po prostu
            // nie zadziala dla tego bohatera w tym odczycie.
        }
        it = typical_cache_.emplace(hero_id, std::move(entry)).first;
    }
    return it->second ? &*it->second : nullptr;
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

    // Matcher bohaterow jest opcjonalny dla odczytu: probujemy go zbudowac,
    // ale blad (siec) nie przerywa odczytu — brak tylko tozsamosci i tie-breaka.
    if (!hero_matcher_) {
        try {
            ensure_hero_matcher();
        } catch (const std::exception&) {
        }
    }
    ScoreboardRead sb =
        read_scoreboard(frame, calib, *matcher_, index_,
                        hero_matcher_ ? hero_matcher_.get() : nullptr, heroes_.get());

    // Tie-break niepewnych itemow po typowym buildzie rozpoznanego bohatera.
    for (auto& row : sb.enemies) {
        if (!row.hero_confident || row.hero_id == 0 || row.uncertain == 0)
            continue;
        if (const auto* typical = typical_items(row.hero_id, row.hero_name))
            apply_typical_tiebreak(row, *typical, index_);
    }

    LiveResult out;
    out.total_items = sb.total_items;
    out.uncertain = sb.uncertain;
    out.objective_name = obj_.name;
    out.enemies = make_views(sb.enemies, prev_enemy_);
    out.allies = make_views(sb.allies, prev_ally_);

    // Profil wroga: baza z KLAS rozpoznanych bohaterow (jak `counter` z nazw
    // wpisanych recznie), doostrzona REALNYMI itemami ze scoreboardu.
    // Sojusznicy wyswietlani informacyjnie (kto z kim walczy, co juz maja).
    std::vector<HeroProfile> enemy_profiles;
    for (const auto& e : sb.enemies)
        if (e.hero_confident)
            if (const HeroProfile* p = heroes_->by_id(e.hero_id))
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

DraftRead VisionSession::read_draft_frame(const cv::Mat& frame, const Calibration& calib) {
    ensure_hero_matcher();
    return read_draft(frame, calib, *hero_matcher_, *heroes_);
}

AutoCalibResult VisionSession::auto_calibrate_frame(const cv::Mat& frame) {
    ensure_matcher();
    return auto_calibrate(frame, *matcher_);
}

AutoCalibResult VisionSession::auto_calibrate_frame(const cv::Mat& frame,
                                                    const Calibration& base) {
    ensure_matcher();
    return auto_calibrate(frame, *matcher_, base);
}

} // namespace predeye
