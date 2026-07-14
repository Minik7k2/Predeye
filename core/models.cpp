#include "models.hpp"

#include <nlohmann/json.hpp>

namespace predeye {

std::string jstr(const nlohmann::json& j, const std::string& key, const std::string& def) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return def;
    return j[key].get<std::string>();
}

int jint(const nlohmann::json& j, const std::string& key, int def) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return def;
    return static_cast<int>(j[key].get<double>());
}

long long jll(const nlohmann::json& j, const std::string& key, long long def) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return def;
    // hero_id Yurei = 10000000001 nie miesci sie w int32 — stad long long.
    return static_cast<long long>(j[key].get<double>());
}

double jdouble(const nlohmann::json& j, const std::string& key, double def) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return def;
    return j[key].get<double>();
}

double Item::stat(const std::string& key) const {
    auto it = stats.find(key);
    return it == stats.end() ? 0.0 : it->second;
}

std::vector<Item> parse_items(const nlohmann::json& j) {
    std::vector<Item> out;
    if (!j.is_array())
        return out;
    out.reserve(j.size());
    for (const auto& e : j) {
        if (!e.is_object())
            continue;
        Item it;
        it.id = jll(e, "id");
        it.name = jstr(e, "name");
        it.display_name = jstr(e, "display_name");
        it.slug = jstr(e, "slug");
        it.slot_type = jstr(e, "slot_type");
        it.aggression_type = jstr(e, "aggression_type");
        it.hero_class = jstr(e, "hero_class");
        it.total_price = jint(e, "total_price"); // null => 0 => niekupowalny
        it.image = jstr(e, "image");
        if (e.contains("stats") && e["stats"].is_object()) {
            for (const auto& [k, v] : e["stats"].items())
                if (v.is_number())
                    it.stats[k] = v.get<double>();
        }
        out.push_back(std::move(it));
    }
    return out;
}

ItemIndex index_by_id(const std::vector<Item>& items) {
    ItemIndex idx;
    for (const auto& it : items)
        idx.emplace(it.id, it);
    return idx;
}

} // namespace predeye
