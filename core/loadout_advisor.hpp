// core/loadout_advisor — asysta przy wyborze crestu, kolejnosci skillowania
// i eternalsow dla (bohater, rola). Crest i skill order pochodza z najlepszego
// buildu spolecznosci (max upvotes-downvotes, jak typical_build); eternalsy
// z recznej bazy data/eternals.json (API ich nie ma).
#pragma once

#include "hero_context.hpp"
#include "local_data.hpp"
#include "models.hpp"
#include "omeda_client.hpp"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace predeye {

struct LoadoutAdvice {
    std::string hero_name;
    std::string build_title;   // tytul buildu spolecznosci ("" gdy brak)
    std::optional<Item> crest; // crest_id rozwiazany przez indeks itemow
    std::string crest_note_pl; // spolszczenie crestu z pl_items ("" gdy brak)
    // Kolejnosc skillowania: skill_order[i] = ktora umiejetnosc (1..4)
    // wbic na poziomie i+1. Pusta gdy build jej nie ma.
    std::vector<int> skill_order;
    // Eternalsy rekomendowane dla (bohater, rola) z data/eternals.json
    // (wskazniki do LocalData — musi zyc dluzej niz advice).
    std::vector<const Eternal*> eternals;
};

// Z gotowego JSON-a buildow (testowalne na fixtures, bez sieci).
LoadoutAdvice loadout_from_json(const nlohmann::json& builds_json, const ItemIndex& index,
                                const LocalData& local, const std::string& hero_name, Role role);

// Wariant sieciowy: pobiera buildy przez API (preferuje builde dla roli,
// pusty wynik => dowolna rola). Bledy sieci nie rzucaja — zwraca advice
// z samymi eternalsami z bazy lokalnej.
LoadoutAdvice loadout_for(OmedaClient& api, const ItemIndex& index, const LocalData& local,
                          const HeroProfile& hero, Role role);

} // namespace predeye
