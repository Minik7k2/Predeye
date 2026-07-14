// core/local_data — reczne dane wlasciciela wersjonowane w repo (katalog data/):
// kontry draftowe, eternalsy (ikony + opisy PL), spolszczenie itemow i pula
// bohaterow do sugestii pickow. API Omeda.city NIE ma tych danych (eternalsy,
// kontry, polskie opisy) — zrodlem jest wlasciciel; pliki sa proste do recznej
// edycji. Brak pliku/wpisu NIGDY nie przerywa programu: puste dane
// + ostrzezenie na stderr.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace predeye {

// Jeden efekt itemu (pasywka/aktywka) z tlumaczeniem. Do wyswietlenia bierz
// `pl`, a gdy puste (nowy item po patchu, nieprzetlumaczony) — `en`.
struct PlEffect {
    std::string name; // nazwa efektu z API (np. "Blighted Spells")
    std::string pl;   // polski opis dzialania
    std::string en;   // oczyszczony opis z API (fallback)
};

struct ItemPl {
    std::string slug, name;
    std::string summary_pl;        // krotkie "co robi" (profil ze statow/tagow)
    std::vector<PlEffect> effects; // "jak dziala" — mechanika pasywek/aktywek
    // Opis efektu z fallbackiem PL -> EN.
    static const std::string& text(const PlEffect& e) { return e.pl.empty() ? e.en : e.pl; }
};

// Wpis recznej bazy kontr draftowych. Bohaterowie po slugu albo display_name
// (dopasowanie lowercase — rozstrzyga HeroDB w miejscu uzycia).
struct CounterEntry {
    std::string hero;
    std::vector<std::string> counters;     // kogo ten bohater kontruje
    std::vector<std::string> countered_by; // kto kontruje jego
    std::string note_pl;                   // uzasadnienie pokazywane w poradzie
};

struct EternalRecommendation {
    std::string hero; // slug/display_name; "" = dowolny bohater
    std::string role; // "midlane"... ; "" = dowolna rola
    std::string note_pl;
};

// Eternal/perk — byt spoza API (baza wlasna: ikony z zrzutow, tresc z pred.gg).
struct Eternal {
    std::string id, name;
    std::string slot; // wg UI gry, np. "primary" / "minor"
    std::string description_pl;
    std::string icon; // sciezka wzgledem data/ (np. "eternals/icons/x.png")
    std::vector<EternalRecommendation> recommended_for;
};

struct HeroPool {
    std::string role;                 // glowna rola wlasciciela ("midlane"...)
    std::vector<std::string> heroes;  // pula do sugestii pickow (slug/nazwa)
};

class LocalData {
  public:
    // Wczytuje wszystkie pliki z katalogu danych; kazdy jest opcjonalny.
    explicit LocalData(std::string data_dir = default_data_dir());

    // env PREDEYE_DATA_DIR, a gdy brak — pierwszy istniejacy z: data,
    // ../data, ../../data, ../../../data (uruchamianie z korzenia repo
    // albo z build/app|gui). Gdy nic nie istnieje: "data".
    static std::string default_data_dir();

    const std::string& dir() const { return dir_; }

    const ItemPl* item_pl(const std::string& slug) const;
    const CounterEntry* counter_entry(const std::string& hero) const; // case-insensitive
    const std::vector<Eternal>& eternals() const { return eternals_; }
    // Eternalsy rekomendowane dla (bohater, rola); wpisy z pustym hero/role
    // pasuja do wszystkiego. Dopasowanie case-insensitive.
    std::vector<const Eternal*> eternals_for(const std::string& hero,
                                             const std::string& role) const;
    const HeroPool& hero_pool() const { return pool_; }

    int items_pl_count() const { return static_cast<int>(items_pl_.size()); }
    int counters_count() const { return static_cast<int>(counters_.size()); }

  private:
    std::string dir_;
    std::map<std::string, ItemPl> items_pl_;       // klucz: slug
    std::map<std::string, CounterEntry> counters_; // klucz: lowercase hero
    std::vector<Eternal> eternals_;
    HeroPool pool_;
};

} // namespace predeye
