// core/shopping_advisor — "co kupic w nastepnej kolejce i dlaczego".
// Cel = wynik counter_build; od celu odejmujemy itemy, ktore juz mamy
// (odczytane z wlasnego wiersza scoreboardu), a uzasadnienia skladamy po
// polsku z powodow silnika (tagi counter, top-staty) i spolszczenia itemow
// (data/pl_items.json).
#pragma once

#include "build_engine.hpp"
#include "local_data.hpp"
#include "models.hpp"

#include <string>
#include <vector>

namespace predeye {

struct NextPurchase {
    Item item;
    std::string reason_pl; // pelne uzasadnienie (counter/efektywnosc + co robi)
};

struct ShoppingAdvice {
    std::vector<NextPurchase> queue; // brakujace itemy celu w kolejnosci zakupu
    std::vector<Item> owned_in_plan; // itemy celu, ktore juz mamy
    int remaining_cost = 0;          // laczny koszt brakujacych
};

// Przetlumacz `PickedItem::reason` silnika na polski (tagi counter i nazwy
// statow); dokleja summary_pl/pierwszy efekt z pl_items, gdy dostepne.
std::string purchase_reason_pl(const PickedItem& pick, const LocalData& local);

// Kolejka zakupow: itemy z `counter`, ktorych nie ma w `owned`, w kolejnosci
// silnika (najpierw itemy-countery z fazy 1, potem wg efektywnosci).
ShoppingAdvice next_purchases(const BuildResult& counter, const std::vector<Item>& owned,
                              const LocalData& local);

} // namespace predeye
