#include "build_engine.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

namespace predeye {
namespace {

// Sito statyczne (§6.4): kupowalny, Passive/Crest, zgodnosc klasowa.
bool sieve_ok(const Item& it, const Objective& obj) {
    if (!it.buyable())
        return false;
    if (it.slot_type != "Passive" && it.slot_type != "Crest")
        return false;
    if (!obj.hero_class.empty() && !it.hero_class.empty() && it.hero_class != "None" &&
        it.hero_class != obj.hero_class)
        return false;
    return true;
}

std::string fmt_stat(const std::string& name, double v) {
    char buf[64];
    if (v == static_cast<long long>(v))
        std::snprintf(buf, sizeof(buf), "%s %lld", name.c_str(), static_cast<long long>(v));
    else
        std::snprintf(buf, sizeof(buf), "%s %.1f", name.c_str(), v);
    return buf;
}

// Top-2 staty itemu do pola reason: najpierw wg wkladu waga*wartosc,
// w razie braku wazonych statow — wg surowej wartosci.
std::string top2_stats(const Item& it, const std::map<std::string, double>& weights) {
    std::vector<std::pair<std::string, double>> ranked;
    for (const auto& [k, v] : it.stats) {
        auto w = weights.find(k);
        double contrib = (w != weights.end()) ? w->second * v : 0.0;
        ranked.emplace_back(k, contrib);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    // Uzupelnij pozycje o zerowym wkladzie sortujac je po surowej wartosci.
    auto zero_from = std::find_if(ranked.begin(), ranked.end(),
                                  [](const auto& p) { return p.second <= 0.0; });
    std::stable_sort(zero_from, ranked.end(), [&](const auto& a, const auto& b) {
        return it.stat(a.first) > it.stat(b.first);
    });
    std::string out;
    for (size_t i = 0; i < ranked.size() && i < 2; ++i) {
        if (i)
            out += ", ";
        out += fmt_stat(ranked[i].first, it.stat(ranked[i].first));
    }
    return out.empty() ? "brak statow" : out;
}

} // namespace

BuildEngine::BuildEngine(std::vector<Item> items) : items_(std::move(items)) {}

double BuildEngine::score(const Item& it, const std::map<std::string, double>& weights) const {
    double s = 0.0;
    for (const auto& [stat, w] : weights)
        s += w * it.stat(stat);
    return s;
}

BuildResult BuildEngine::assemble(const Objective& obj,
                                  const std::map<std::string, double>& weights,
                                  const std::vector<std::string>& required_tags) const {
    BuildResult res;
    std::set<long long> used;
    int spent = 0;
    bool crest_used = false;

    auto fits = [&](const Item& it) {
        if (static_cast<int>(res.items.size()) >= obj.max_items)
            return false;
        if (used.count(it.id))
            return false;
        if (it.slot_type == "Crest" && crest_used)
            return false;
        return spent + it.total_price <= obj.budget;
    };
    auto take = [&](const Item& it, double s, std::string reason) {
        used.insert(it.id);
        spent += it.total_price;
        if (it.slot_type == "Crest")
            crest_used = true;
        res.items.push_back({it, s, s / std::max(1, it.total_price), std::move(reason)});
    };

    // Faza 1: po jednym najbardziej oplacalnym itemie na kazdy wymagany tag.
    for (const auto& tag : required_tags) {
        const Item* best = nullptr;
        double best_score = 0.0, best_eff = -1.0;
        for (const auto& it : items_) {
            if (!sieve_ok(it, obj) || it.aggression_type != tag || !fits(it))
                continue;
            double s = score(it, weights);
            double eff = s / std::max(1, it.total_price);
            if (eff > best_eff) {
                best = &it;
                best_eff = eff;
                best_score = s;
            }
        }
        if (!best) {
            // Ulepszenie wzgledem prototypu: jawne ostrzezenie zamiast cichego pominiecia.
            std::fprintf(stderr, "predeye: uwaga: brak itemu counter dla taga %s (klasa: %s)\n",
                         tag.c_str(), obj.hero_class.empty() ? "-" : obj.hero_class.c_str());
            continue;
        }
        take(*best, best_score, "counter: " + tag + " (" + top2_stats(*best, weights) + ")");
    }

    // Faza 2: zachlannie wg efficiency malejaco; odrzucenie kandydata
    // (np. za drogi) nie przerywa petli.
    std::vector<std::pair<double, const Item*>> ranked;
    for (const auto& it : items_) {
        if (!sieve_ok(it, obj))
            continue;
        double s = score(it, weights);
        if (s <= 0.0)
            continue;
        ranked.emplace_back(s / std::max(1, it.total_price), &it);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [eff, itp] : ranked) {
        if (static_cast<int>(res.items.size()) >= obj.max_items)
            break;
        if (!fits(*itp))
            continue;
        double s = score(*itp, weights);
        take(*itp, s, top2_stats(*itp, weights));
        (void)eff;
    }

    res.total_cost = spent;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %d itemow za %d zlota (budzet %d)", obj.name.c_str(),
                  static_cast<int>(res.items.size()), spent, obj.budget);
    res.summary = buf;
    return res;
}

BuildResult BuildEngine::optimize(const Objective& obj) const {
    return assemble(obj, obj.weights, {});
}

BuildResult BuildEngine::counter_build(const Objective& obj, const EnemyProfile& enemy) const {
    auto weights = obj.weights;
    // Obrona proporcjonalna do profilu obrazen wroga (§6.4).
    weights["physical_armor"] += 0.8 * enemy.physical_ratio;
    weights["magical_armor"] += 0.8 * (1.0 - enemy.physical_ratio);
    if (enemy.has_tanks) {
        weights["physical_penetration"] += 0.6;
        weights["magical_penetration"] += 0.6;
    }
    std::vector<std::string> tags;
    if (enemy.has_healing)
        tags.push_back("AntiHeal");
    if (enemy.has_crit)
        tags.push_back("AntiCrit");
    if (enemy.has_tanks)
        tags.push_back("AntiTank");
    return assemble(obj, weights, tags);
}

} // namespace predeye
