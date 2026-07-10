// core/build_engine — liniowy silnik doboru itemow: optimize / counter_build.
#pragma once

#include "models.hpp"

#include <map>
#include <string>
#include <vector>

namespace predeye {

struct Objective {
    std::string name;
    std::map<std::string, double> weights;
    int budget = 12000;
    int max_items = 5;
    std::string hero_class; // "" = bez ograniczenia klasowego
};

struct EnemyProfile {
    double physical_ratio = 0.5; // 1.0 = czysto fizyczny sklad wroga
    bool has_healing = false, has_crit = false, has_tanks = false;
};

struct PickedItem {
    Item item;
    double score = 0.0, efficiency = 0.0; // efficiency = score / cena
    std::string reason;
};

struct BuildResult {
    std::vector<PickedItem> items;
    int total_cost = 0;
    std::string summary;
};

class BuildEngine {
  public:
    explicit BuildEngine(std::vector<Item> items);

    double score(const Item& it, const std::map<std::string, double>& weights) const;

    BuildResult optimize(const Objective& obj) const;
    BuildResult counter_build(const Objective& obj, const EnemyProfile& enemy) const;

  private:
    BuildResult assemble(const Objective& obj, const std::map<std::string, double>& weights,
                         const std::vector<std::string>& required_tags) const;

    std::vector<Item> items_;
};

} // namespace predeye
