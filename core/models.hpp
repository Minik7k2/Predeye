// core/models — struktury danych z API Omeda.city + defensywne parsowanie.
#pragma once

#include <map>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace predeye {

// Pomocnicze gettery odporne na null / brak pola — API bywa dziurawe (§5).
std::string jstr(const nlohmann::json& j, const std::string& key,
                 const std::string& def = "");
int jint(const nlohmann::json& j, const std::string& key, int def = 0);
long long jll(const nlohmann::json& j, const std::string& key, long long def = 0);
double jdouble(const nlohmann::json& j, const std::string& key, double def = 0.0);

struct Item {
    long long id = 0;
    std::string name, display_name;
    std::string slug; // stabilny klucz czytelny dla czlowieka (data/pl_items.json)
    std::string slot_type, aggression_type, hero_class;
    int total_price = 0; // 0 = niekupowalny (bazowe cresty, stadia)
    std::map<std::string, double> stats;
    std::string image; // "/assets/....webp" — pelny URL: base + image

    bool buyable() const { return total_price > 0; }
    double stat(const std::string& key) const; // 0.0 gdy brak
};

std::vector<Item> parse_items(const nlohmann::json& j);

using ItemIndex = std::map<long long, Item>;
ItemIndex index_by_id(const std::vector<Item>& items);

} // namespace predeye
