#include "local_data.hpp"

#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

namespace predeye {
namespace {

namespace fs = std::filesystem;

// Kanoniczna forma nazwy bohatera: lowercase, znaki nie-alfanumeryczne -> "-".
// Dzieki temu display_name ("The Fey", "GRIM.exe") i slug ("the-fey",
// "grim-exe") wskazuja ten sam wpis w recznych plikach danych.
std::string canon_hero(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
        out += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '-';
    return out;
}

// Wczytaj JSON z pliku; brak pliku => nullopt bez halasu (pliki sa opcjonalne),
// uszkodzony plik => nullopt + ostrzezenie (wlasciciel edytuje je recznie).
std::optional<nlohmann::json> load_json(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    try {
        return nlohmann::json::parse(in);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "predeye: uwaga: %s — niepoprawny JSON (%s); pomijam.\n",
                     path.string().c_str(), ex.what());
        return std::nullopt;
    }
}

std::vector<std::string> jstrlist(const nlohmann::json& j, const std::string& key) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(key) || !j[key].is_array())
        return out;
    for (const auto& e : j[key])
        if (e.is_string())
            out.push_back(e.get<std::string>());
    return out;
}

} // namespace

std::string LocalData::default_data_dir() {
    if (const char* env = std::getenv("PREDEYE_DATA_DIR"); env && *env)
        return env;
    for (const char* cand : {"data", "../data", "../../data", "../../../data"}) {
        std::error_code ec;
        if (fs::is_directory(cand, ec))
            return cand;
    }
    return "data";
}

LocalData::LocalData(std::string data_dir) : dir_(std::move(data_dir)) {
    const fs::path base(dir_);

    if (auto j = load_json(base / "pl_items.json")) {
        const auto& items = (*j).contains("items") ? (*j)["items"] : nlohmann::json();
        if (items.is_object()) {
            for (const auto& [slug, e] : items.items()) {
                if (!e.is_object())
                    continue;
                ItemPl it;
                it.slug = slug;
                it.name = jstr(e, "name");
                it.summary_pl = jstr(e, "summary_pl");
                if (e.contains("effects") && e["effects"].is_array()) {
                    for (const auto& ef : e["effects"]) {
                        if (!ef.is_object())
                            continue;
                        it.effects.push_back(
                            PlEffect{jstr(ef, "name"), jstr(ef, "pl"), jstr(ef, "en")});
                    }
                }
                items_pl_.emplace(slug, std::move(it));
            }
        }
    }

    if (auto j = load_json(base / "counters.json")) {
        const auto& entries = (*j).contains("entries") ? (*j)["entries"] : nlohmann::json();
        if (entries.is_array()) {
            for (const auto& e : entries) {
                if (!e.is_object())
                    continue;
                CounterEntry c;
                c.hero = jstr(e, "hero");
                c.counters = jstrlist(e, "counters");
                c.countered_by = jstrlist(e, "countered_by");
                c.note_pl = jstr(e, "note_pl");
                if (!c.hero.empty())
                    counters_.emplace(canon_hero(c.hero), std::move(c));
            }
        }
    }

    if (auto j = load_json(base / "eternals.json")) {
        const auto& list = (*j).contains("eternals") ? (*j)["eternals"] : nlohmann::json();
        if (list.is_array()) {
            for (const auto& e : list) {
                if (!e.is_object())
                    continue;
                Eternal et;
                et.id = jstr(e, "id");
                et.name = jstr(e, "name");
                et.slot = jstr(e, "slot");
                et.description_pl = jstr(e, "description_pl");
                et.icon = jstr(e, "icon");
                if (e.contains("recommended_for") && e["recommended_for"].is_array()) {
                    for (const auto& r : e["recommended_for"]) {
                        if (!r.is_object())
                            continue;
                        et.recommended_for.push_back(EternalRecommendation{
                            jstr(r, "hero"), jstr(r, "role"), jstr(r, "note_pl")});
                    }
                }
                if (!et.id.empty())
                    eternals_.push_back(std::move(et));
            }
        }
    }

    if (auto j = load_json(base / "hero_pool.json")) {
        pool_.role = jstr(*j, "role");
        pool_.heroes = jstrlist(*j, "heroes");
    }
}

const ItemPl* LocalData::item_pl(const std::string& slug) const {
    auto it = items_pl_.find(slug);
    return it == items_pl_.end() ? nullptr : &it->second;
}

const CounterEntry* LocalData::counter_entry(const std::string& hero) const {
    auto it = counters_.find(canon_hero(hero));
    return it == counters_.end() ? nullptr : &it->second;
}

std::vector<const Eternal*> LocalData::eternals_for(const std::string& hero,
                                                    const std::string& role) const {
    const std::string h = canon_hero(hero), r = canon_hero(role);
    std::vector<const Eternal*> out;
    for (const auto& e : eternals_) {
        for (const auto& rec : e.recommended_for) {
            const bool hero_ok = rec.hero.empty() || canon_hero(rec.hero) == h;
            const bool role_ok = rec.role.empty() || canon_hero(rec.role) == r;
            if (hero_ok && role_ok) {
                out.push_back(&e);
                break;
            }
        }
    }
    return out;
}

} // namespace predeye
