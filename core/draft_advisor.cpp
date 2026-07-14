#include "draft_advisor.hpp"

#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

namespace predeye {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Kanoniczna (lowercase display_name) forma nazwy — kontry i pula moga uzywac
// slugow albo nazw; HeroDB rozstrzyga jedne i drugie.
std::string canon(const HeroDB& db, const std::string& name) {
    if (const HeroProfile* h = db.by_name(name))
        return lower(h->name);
    return lower(name);
}

std::set<std::string> canon_set(const HeroDB& db, const std::vector<std::string>& names) {
    std::set<std::string> out;
    for (const auto& n : names)
        out.insert(canon(db, n));
    return out;
}

std::string pct(double v01) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f%%", 100.0 * v01);
    return buf;
}

} // namespace

std::vector<MetaHeroStat> parse_meta(const nlohmann::json& meta_json, const HeroDB& heroes) {
    std::vector<MetaHeroStat> out;
    const nlohmann::json* arr = &meta_json;
    if (meta_json.is_object() && meta_json.contains("hero_statistics"))
        arr = &meta_json["hero_statistics"];
    if (!arr->is_array())
        return out;

    for (const auto& e : *arr) {
        if (!e.is_object())
            continue;
        MetaHeroStat s;
        s.hero_id = jll(e, "hero_id");
        // Wiersz-agregat / smieciowe id: brak w HeroDB => odfiltruj (§5).
        const HeroProfile* h = heroes.by_id(s.hero_id);
        if (!h)
            continue;
        s.name = h->name; // API daje "Unknown" — nazwa z HeroDB
        s.winrate = jdouble(e, "winrate") / 100.0;     // skala 0-100 -> 0-1
        s.pickrate = jdouble(e, "pickrate") / 100.0;
        s.matches = jdouble(e, "match_count");
        out.push_back(std::move(s));
    }
    return out;
}

DraftAdvisor::DraftAdvisor(const HeroDB& heroes, const LocalData& local,
                           std::vector<MetaHeroStat> meta)
    : heroes_(heroes), local_(local), meta_(std::move(meta)) {}

const MetaHeroStat* DraftAdvisor::meta_for(const std::string& hero) const {
    const std::string key = canon(heroes_, hero);
    for (const auto& m : meta_)
        if (lower(m.name) == key)
            return &m;
    return nullptr;
}

std::vector<DraftSuggestion> DraftAdvisor::suggest_bans(const std::vector<std::string>& excluded,
                                                        int count) const {
    const auto skip = canon_set(heroes_, excluded);

    // Bohaterowie kontrujacy moja pule (wg counters.json, w obu kierunkach).
    std::set<std::string> threats;
    for (const auto& mine : local_.hero_pool().heroes) {
        if (const CounterEntry* e = local_.counter_entry(mine))
            for (const auto& c : e->countered_by)
                threats.insert(canon(heroes_, c));
        // Kierunek odwrotny: wpisy innych bohaterow z moim w "counters".
        for (const auto& h : heroes_.all()) {
            const CounterEntry* other = local_.counter_entry(h.name);
            if (!other)
                other = local_.counter_entry(h.slug);
            if (!other)
                continue;
            for (const auto& c : other->counters)
                if (canon(heroes_, c) == canon(heroes_, mine))
                    threats.insert(lower(h.name));
        }
    }

    std::vector<DraftSuggestion> out;
    for (const auto& m : meta_) {
        const std::string key = lower(m.name);
        if (skip.count(key))
            continue;
        // Sila mety: przewaga winrate nad 50% wazona popularnoscia (pickrate
        // tlumi szum rzadko granych). Kontra na moja pule podbija range.
        const double meta_strength = (m.winrate - 0.5) * std::sqrt(std::max(0.0, m.pickrate));
        const bool threat = threats.count(key) > 0;
        DraftSuggestion s;
        s.hero = m.name;
        s.score = meta_strength + (threat ? 0.05 : 0.0);
        s.reason_pl = "winrate " + pct(m.winrate) + ", pickrate " + pct(m.pickrate);
        if (threat)
            s.reason_pl += "; kontruje bohaterow z Twojej puli (counters.json)";
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(out.size()) > count)
        out.resize(static_cast<size_t>(count));
    return out;
}

std::vector<DraftSuggestion>
DraftAdvisor::suggest_picks(const std::vector<std::string>& enemy_picks,
                            const std::vector<std::string>& excluded, int count) const {
    const auto skip = canon_set(heroes_, excluded);
    const auto enemy = canon_set(heroes_, enemy_picks);

    // Profil obrazen widocznego skladu wroga — miks trudniej zitemizowac,
    // monokultura (100% fiz/mag) ulatwia obrone; premiuj picki roznicujace.
    std::vector<HeroProfile> enemy_profiles;
    for (const auto& e : enemy_picks)
        if (const HeroProfile* h = heroes_.by_name(e))
            enemy_profiles.push_back(*h);
    const EnemyProfile eprof = enemy_from(enemy_profiles);

    std::vector<DraftSuggestion> out;
    for (const auto& mine : local_.hero_pool().heroes) {
        const HeroProfile* h = heroes_.by_name(mine);
        if (!h)
            continue;
        const std::string key = lower(h->name);
        if (skip.count(key) || enemy.count(key))
            continue;

        DraftSuggestion s;
        s.hero = h->name;
        std::string why;

        // Forma z mety.
        if (const MetaHeroStat* m = meta_for(h->name)) {
            s.score += (m->winrate - 0.5);
            why = "winrate " + pct(m->winrate);
        }

        // Kontry wzgledem widocznych pickow wroga (counters.json).
        if (const CounterEntry* e = local_.counter_entry(h->name)) {
            for (const auto& c : e->counters) {
                if (enemy.count(canon(heroes_, c))) {
                    s.score += 0.08;
                    why += (why.empty() ? "" : "; ") + std::string("kontruje ") + c;
                    if (!e->note_pl.empty())
                        why += " (" + e->note_pl + ")";
                }
            }
            for (const auto& c : e->countered_by) {
                if (enemy.count(canon(heroes_, c))) {
                    s.score -= 0.08;
                    why += (why.empty() ? "" : "; ") + std::string("uwaga: kontruje go ") + c;
                }
            }
        }

        // Bilans obrazen: gdy wrog jest mocno fizyczny, bohater magiczny
        // utrudnia im itemizacje obrony (i odwrotnie).
        if (!enemy_profiles.empty()) {
            const bool i_am_magical = h->damage == DamageType::Magical;
            const bool i_am_physical = h->damage == DamageType::Physical;
            if (eprof.physical_ratio >= 0.75 && i_am_magical) {
                s.score += 0.03;
                why += (why.empty() ? "" : "; ") +
                       std::string("obrazenia magiczne vs fizyczny sklad wroga");
            } else if (eprof.physical_ratio <= 0.25 && i_am_physical) {
                s.score += 0.03;
                why += (why.empty() ? "" : "; ") +
                       std::string("obrazenia fizyczne vs magiczny sklad wroga");
            }
        }

        s.reason_pl = why.empty() ? "brak danych meta/kontr — pick z puli" : why;
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(out.size()) > count)
        out.resize(static_cast<size_t>(count));
    return out;
}

} // namespace predeye
