// core/hero_context — profile bohaterow z klas + presety celow per rola.
#pragma once

#include "build_engine.hpp"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace predeye {

enum class DamageType { Physical, Magical, Mixed };
enum class Role { Offlane, Jungle, Midlane, Carry, Support, Unknown };

Role parse_role(const std::string& s); // case-insensitive; Unknown gdy nie pasuje
std::string role_name(Role r);

struct HeroProfile {
    long long id = 0;
    std::string name; // display_name
    std::string slug;
    std::string image; // "/assets/....webp" — portret (uzywa go vision/hero_matcher)
    std::vector<std::string> classes, roles;
    DamageType damage = DamageType::Physical;
    bool deals_crit = false, can_heal = false, is_tanky = false;
};

class HeroDB {
  public:
    explicit HeroDB(const nlohmann::json& heroes_json);

    // Klucz: lowercase display_name ORAZ slug; nullptr gdy nieznany.
    const HeroProfile* by_name(const std::string& name) const;
    // Nieznana nazwa => wyjatek z podpowiedzia najblizszych nazw.
    std::vector<HeroProfile> by_names(const std::vector<std::string>& names) const;
    const HeroProfile* by_id(long long id) const;

    // Najblizsze znane nazwy (edit distance) — do komunikatow bledow.
    std::vector<std::string> suggest(const std::string& name, size_t count = 3) const;

    const std::vector<HeroProfile>& all() const { return heroes_; }

  private:
    std::vector<HeroProfile> heroes_;
};

// Preset celu dla (bohater, rola); budget_override < 0 => budzet z presetu.
Objective objective_for(const HeroProfile& me, Role role, int budget_override = -1);

// Zgrubny profil skladu wroga z samych klas bohaterow.
EnemyProfile enemy_from(const std::vector<HeroProfile>& enemies);

} // namespace predeye
