# CLAUDE.md — predeye: asystent-trener do gry Predecessor (C++)

Ten dokument jest kompletną instrukcją dla Claude Code. Opisuje CO zbudować,
JAK, w jakiej kolejności i czego NIE robić. Projekt ma za sobą zwalidowany
prototyp — część decyzji jest już rozstrzygnięta pomiarami i nie podlega
dyskusji (oznaczone „WIĄŻĄCE").

Język pracy: komunikacja, komentarze i dokumentacja po polsku; identyfikatory,
nazwy plików i commity po angielsku (conventional commits: `feat:`, `fix:`...).

---

## 1. Czym jest ten projekt

`predeye` to **doradca** gracza Predecessora (MOBA, Windows), działający obok
gry. Trzy filary:

1. **Mózg buildów** — z publicznego API Omeda.city generuje build pod cel
   (bohater + rola) i counter-build pod konkretny skład wroga oraz analizuje,
   jak wrogowie typowo się itemizują.
2. **Oczy** — odczyt realnych itemów wroga z ekranu (scoreboard pod TAB) przez
   zrzut ekranu + dopasowanie ikon. Bez treningu sieci — metoda zmierzona.
3. **Porady** — tryb `live`: po każdym odczycie scoreboardu aktualny
   counter-build i różnice względem poprzedniego odczytu, w konsoli.

Czym projekt **NIE jest**: botem, makrem, cheatem, narzędziem automatyzującym
grę. Nie gra za użytkownika, nie wysyła żadnego wejścia do gry.

---

## 2. Stan wyjściowy

- Jeśli w repo istnieje katalog `reference/`, zawiera działające pliki
  prototypu (kompilowane i uruchamiane na żywym API):
  `models.hpp`, `omeda_client.{hpp,cpp}`, `build_engine.{hpp,cpp}`,
  `hero_context.{hpp,cpp}`, `enemy_build.{hpp,cpp}`, `main.cpp`,
  `icon_match_test.cpp`. Traktuj je jako źródło prawdy co do zachowań —
  przenieś do docelowej struktury (§7) z refaktorem, zachowując kontrakty.
- Jeśli katalogu brak — zaimplementuj moduły 1:1 według §6.

**WIĄŻĄCE wyniki pomiarów z prototypu** (walidacja na 140 realnych ikonach
z API, z symulacją zniekształceń capture):

| metoda                  | top-1 (realist.) | top-1 (pesym.) | top-3 |
|-------------------------|------------------|----------------|-------|
| kolor-NCC 32×32         | 99,7%            | 95,6%          | 100%  |
| dHash-64                | 87,9%            | 67,1%          | —     |

Wniosek WIĄŻĄCY: rozpoznawanie itemów robimy **kolor-NCC 32×32** (opis §6.6).
dHash odrzucony pomiarem — nie wracać do niego.

---

## 3. Twarde zasady (nienegocjowalne)

1. **Zero ingerencji w grę.** Żadnego wysyłania inputu, czytania pamięci
   procesu, wstrzykiwania DLL, hookowania. Jedyne wejście wizualne to zrzut
   ekranu przez publiczne API systemu (DXGI Desktop Duplication).
2. **Narzędzie prywatne** właściciela repo, do użytku osobistego.
3. **Omeda.city API jest nieoficjalne i bez gwarancji.** Własny User-Agent
   (`predeye/<wersja> (osobisty trener; rozsadne uzycie)`), cache dyskowy
   z TTL (§5), pauza ≥ 0,3 s między ŻYWYMI zapytaniami. Testy automatyczne
   chodzą na fixtures (§9), nie na żywym API.
4. **Czysty build** na wysokim poziomie ostrzeżeń (MSVC `/W4` lub
   `-Wall -Wextra`). Ostrzeżenie = do naprawy przed commitem.
5. **Pytaj właściciela zanim:** dodasz zależność spoza listy (§7), zmienisz
   publiczny interfejs modułu, zaczniesz M6/M7, podejmiesz decyzję UI/UX.

---

## 4. Architektura i przepływ

```
[Omeda.city API]        [wejście gracza: bohater, rola, wrogowie]
       |                                   |
 [omeda_client: libcurl + cache TTL]       |
       |                                   v
 [models: Item / HeroDB]  --->  [hero_context: Objective + EnemyProfile]
       |                                   |
 [enemy_build: typowe buildy wrogów -> doostrzenie profilu]
       |                                   |
       +----------------> [build_engine: optimize / counter_build]
                                           |
                                     [wynik + powody]

Tor wizyjny (M2–M5):
 [capture DXGI] -> [calibration: ROI slotów] -> [icon_matcher NCC]
      -> realne itemy wroga -> (ten sam) enemy_build -> counter na żywo
```

Kontrakt wejścia rdzenia jest stały: `(bohater, rola, wrogowie[, itemy])`.
Dziś wypełnia go CLI, docelowo ekran. Rdzeń (`core/`) musi pozostać
przenośny (kompilowalny na Linux/macOS); Windows-only jest tylko `vision/capture`.

---

## 5. Dane zewnętrzne: Omeda.city API

Base URL: `https://omeda.city`. Wszystkie ścieżki zwracają JSON, bez auth.

| Endpoint | Zawartość | TTL cache |
|---|---|---|
| `/heroes.json` | 52 bohaterów: id, display_name, slug, classes[], roles[], image | 24 h |
| `/items.json` | ~270 itemów (213 kupowalnych): patrz niżej | 24 h |
| `/builds.json?filter[hero_id]=&filter[role]=&filter[order]=popular&filter[current_version]=1` | buildy społeczności | 1 h |
| `/players.json?filter[name]=` | wyszukiwanie gracza | 1 h |
| `/players/{uuid}.json` | profil gracza | 1 h |
| `/players/{id}/statistics.json?time_frame=` | staty gracza | 1 h |
| `/players/{id}/hero_statistics.json?time_frame=` | staty per bohater | 1 h |
| `/players/{id}/matches.json?per_page=` | historia meczów | 15 min |
| `/dashboard/hero_statistics.json?time_frame=&game_mode=` | meta globalna | 6 h |

`time_frame ∈ {ALL,3M,2M,1M,3W,2W,1W,1D}`, `game_mode ∈ {pvp,ranked,custom,brawl}`.

Pola itemu: `id`, `display_name`, `slot_type ∈ {Passive, Crest, Active,
Trinket}`, `aggression_type` (tagi taktyczne, m.in. `Offense, Defense,
AntiHeal, AntiTank, AntiMagic, AntiCrit, PhysicalShred, Burst, Sustain,
Mobility, AntiBurst, SpellShield, MultiKill`), `hero_class ∈ {Mage, Tank,
Fighter, Sharpshooter, Assassin, Support, None/null,...}`, `total_price`,
`stats{}` (m.in. `physical_power, magical_power, physical_penetration,
magical_penetration, ability_haste, attack_speed, critical_chance, lifesteal,
omnivamp, magical_lifesteal, heal_shield_increase, max_health,
physical_armor, magical_armor, max_mana, mana_regeneration, gold_per_second,
tenacity`), `build_paths` (drzewo komponentów), `image` = `/assets/<hash>.webp`
(pełny URL: base + image).

Rekord buildu: `id, title, hero_id, role, crest_id, item1_id..item6_id,
modules, skill_order, upvotes_count, downvotes_count`.

**Pułapki (WIĄŻĄCE, zweryfikowane eksperymentalnie):**

- `base_stats` bohaterów są BEZUŻYTECZNE do określenia typu obrażeń — każdy
  bohater ma `physical_power`. Typ obrażeń wyprowadzamy z **klasy** (§6.3).
- Opisy umiejętności są za szumne do flag (heal/cc) — nie parsować słów
  kluczowych.
- Endpointy statystyk graczy zwracają `display_name: "Unknown"` dla bohaterów
  — mapować po `hero_id` przez `/heroes.json`.
- Istnieje bohater o id `10000000001` (Yurei) — id nie mieszczą się w int32?
  Mieszczą się w int64; używaj `int64_t`/`long long` na hero_id albo
  udokumentuj założenie.
- W statystykach graczy pojawia się śmieciowe `hero_id = 75` (agregat) —
  odfiltrować (nieznane id + absurdalna liczba gier).
- Skale winrate są NIESPÓJNE: staty gracza 0–1, meta (`/dashboard/...`) 0–100.
  Normalizować przy parsowaniu.
- Nazwy pól meta ≠ gracza: `avg_cs`↔`cs_min`, `avg_gold`↔`gold_min`.
- `total_price == 0` lub null ⇒ item niekupowalny (bazowe cresty, stadia).
- Pola bywają null (`required_level` itd.) — parsować defensywnie.

---

## 6. Specyfikacja modułów

### 6.1 `core/models` — struktury danych

```cpp
struct Item {
  long long id;
  std::string name, display_name;
  std::string slot_type, aggression_type, hero_class;
  int total_price;                       // 0 = niekupowalny
  std::map<std::string,double> stats;
  std::string image;                     // "/assets/....webp"
  bool buyable() const { return total_price > 0; }
  double stat(const std::string&) const; // 0.0 gdy brak
};
std::vector<Item> parse_items(const nlohmann::json&);
std::map<long long, Item> index_by_id(const std::vector<Item>&);
```

Parsowanie defensywne (pomocnicze `jstr/jint/jll` odporne na null/brak pola).

### 6.2 `core/omeda_client`

```cpp
class OmedaClient {
public:
  OmedaClient(std::string cache_dir = default_dir(), long default_ttl_s = 21600);
  nlohmann::json get(const std::string& path, long ttl_override = -1);
  nlohmann::json items();   // TTL 24h
  nlohmann::json heroes();  // TTL 24h
  nlohmann::json builds(long long hero_id, const std::string& role /*""=dowolna*/,
                        const std::string& order = "popular");   // TTL 1h
};
```

- libcurl easy, `CURLOPT_ACCEPT_ENCODING ""`, timeout 30 s, follow redirects.
- Cache: plik JSON per URL (nazwa = hash ścieżki) w
  `%LOCALAPPDATA%/predeye/cache` (Windows) / `~/.cache/predeye` (inne);
  uszkodzony plik cache ⇒ pobierz na nowo.
- Pauza ≥ 0,3 s między żywymi zapytaniami (licznik w kliencie).
- HTTP ≥ 400 lub błąd sieci ⇒ wyjątek `OmedaError` z czytelnym komunikatem.

### 6.3 `core/hero_context`

```cpp
enum class DamageType { Physical, Magical, Mixed };
enum class Role { Offlane, Jungle, Midlane, Carry, Support, Unknown };

struct HeroProfile {
  long long id; std::string name;
  std::vector<std::string> classes, roles;
  DamageType damage; bool deals_crit, can_heal, is_tanky;
};
class HeroDB {                 // klucz: lowercase display_name ORAZ slug
  const HeroProfile* by_name(const std::string&) const;
  std::vector<HeroProfile> by_names(const std::vector<std::string>&) const;
};
Objective   objective_for(const HeroProfile& me, Role role, int budget_override=-1);
EnemyProfile enemy_from(const std::vector<HeroProfile>& enemies);
```

Wyprowadzenie profilu z klas (WIĄŻĄCE, coarse ale deterministyczne):
`Mage → Magical`; `Sharpshooter/Assassin/Fighter/Executioner → Physical`;
`Enchanter/Support/Catcher → Mixed (ratio 0.5)`; reszta (Tank/Warden itd.)
→ Physical. `deals_crit = Sharpshooter`; `can_heal = Enchanter|Support`;
`is_tanky = Tank|Warden`.

Presety `objective_for` (power/pen = magical_* gdy Magical, inaczej physical_*):

| Rola | wagi | budżet |
|---|---|---|
| Carry | power 1.0, attack_speed 0.8, lifesteal 0.4, pen 1.0, (+critical_chance 1.2 gdy Sharpshooter) | 14000 |
| Midlane | power 1.0, pen 2.5, ability_haste 0.6 | 12000 |
| Jungle | power 1.0, pen 1.5, ability_haste 0.5, omnivamp 0.4 | 12000 |
| Offlane | power 0.8, max_health 0.04, ability_haste 0.6, pen 0.8 | 12000 |
| Support | heal_shield_increase 1.5, ability_haste 1.0, max_health 0.04, gold_per_second 1.5, tenacity 0.4; hero_class="" | 9000 |

`enemy_from`: `physical_ratio` = średnia (Physical=1, Magical=0, Mixed=0.5);
flagi OR po `can_heal/deals_crit/is_tanky`.

### 6.4 `core/build_engine`

```cpp
struct Objective { std::string name; std::map<std::string,double> weights;
                   int budget = 12000; int max_items = 5; std::string hero_class; };
struct EnemyProfile { double physical_ratio = 0.5;
                      bool has_healing=false, has_crit=false, has_tanks=false; };
struct PickedItem { Item item; double score, efficiency; std::string reason; };
struct BuildResult { std::vector<PickedItem> items; int total_cost; std::string summary; };

class BuildEngine {
  BuildResult optimize(const Objective&) const;
  BuildResult counter_build(const Objective&, const EnemyProfile&) const;
};
```

Algorytm (WIĄŻĄCE):

- `score(item) = Σ_stat weights[stat] * item.stat(stat)`;
  `efficiency = score / max(1, total_price)`.
- Sito statyczne: `buyable()` ∧ `slot_type ∈ {Passive, Crest}` ∧ `class_ok`
  (`obj.hero_class` puste ∨ `item.hero_class ∈ {"", "None", obj.hero_class}`).
  Do fazy 2 dodatkowo `score > 0`.
- `counter_build` przed składaniem modyfikuje wagi:
  `w[physical_armor] += 0.8 * ratio; w[magical_armor] += 0.8 * (1-ratio);`
  gdy `has_tanks`: `w[physical_penetration] += 0.6; w[magical_penetration] += 0.6;`
  oraz buduje tagi: healing→`AntiHeal`, crit→`AntiCrit`, tanks→`AntiTank`.
- Składanie dwufazowe: **faza 1** — dla każdego wymaganego taga jeden
  najbardziej opłacalny item z tym `aggression_type` przechodzący sito;
  **faza 2** — zachłannie wg `efficiency` malejąco. Ograniczenia dynamiczne:
  budżet (odrzucenie kandydata nie przerywa pętli), `max_items`, ≤1 Crest,
  bez duplikatów.
- `reason`: dla fazy 1 `"counter: <Tag> (<top-2 staty>)"`, dla fazy 2 top-2
  staty itemu.
- ULEPSZENIE względem prototypu: gdy tag fazy 1 nie znajdzie żadnego itemu
  (np. AntiCrit dla maga) — zaloguj ostrzeżenie na stderr zamiast cichego
  pominięcia.

Znane, świadome luki v1 (NIE naprawiaj bez zlecenia; backlog w M6):
model liniowy bez synergii, brak EHP (czysta obrona przegrywa z mocą),
pasywki niemodelowane, wynik to zestaw docelowy bez kolejności zakupów.

### 6.5 `core/enemy_build`

```cpp
struct EnemyBuildProfile {
  long long hero_id; std::string hero_name, title;
  std::vector<Item> items;
  double magical_share;                       // z sumy power-statów
  int crit_items, sustain_items, armor_items, attack_speed_items;
  bool crit_heavy()   const { return crit_items    >= 2; }
  bool sustain_heavy()const { return sustain_items >= 1; }
  bool tanky()        const { return armor_items   >= 2; }
  bool is_magical()   const { return magical_share > 0.6; }
};
EnemyBuildProfile classify_items(const std::vector<Item>&);   // wspólna logika
std::optional<EnemyBuildProfile> typical_build(OmedaClient&, const ItemIndex&,
                                               long long hero_id, const std::string& name);
void refine_enemy_profile(EnemyProfile&, const std::vector<EnemyBuildProfile>&);
```

- `typical_build`: pobierz buildy bohatera (rola pusta = dowolna), wybierz
  max `(upvotes − downvotes)`; rozwiń `item1_id..item6_id` przez indeks;
  brak buildów ⇒ `nullopt` (pomiń bohatera, nie wywalaj się).
- Klasyfikacja itemu: kryt gdy `critical_chance>0`; sustain gdy
  `lifesteal|omnivamp|magical_lifesteal|heal_shield_increase > 0`; armor gdy
  `physical_armor|magical_armor|max_health > 0`; `magical_share =
  Σmagical_power / (Σphysical_power + Σmagical_power)` (0.5 gdy suma 0).
- `refine`: `physical_ratio = mean(1 − magical_share)`; flagi OR
  (`crit_heavy → has_crit`, `sustain_heavy → has_healing`, `tanky → has_tanks`).
- WAŻNE: `classify_items` przyjmuje listę `Item` — będzie reużyte w M4 dla
  realnych itemów ze scoreboardu.

### 6.6 `vision/icon_matcher` (WIĄŻĄCA metoda)

```cpp
struct MatchResult { long long item_id; float cosine;
                     std::array<std::pair<long long,float>,3> top3; };
class IconMatcher {
  // buduje bazę sygnatur z ikon; ikony cache'owane na dysku
  explicit IconMatcher(const std::vector<Item>& items, OmedaClient& api,
                       const std::string& icon_cache_dir);
  MatchResult match(const cv::Mat& slot_bgr) const;
  static bool looks_empty(const cv::Mat& slot_bgr);  // heurystyka pustego slotu
};
```

- Sygnatura: `resize` INTER_AREA do **32×32 BGR** → `CV_32F` →
  `reshape(1,1)` → odejmij średnią → podziel przez normę L2. Dopasowanie:
  iloczyn skalarny (cosine == NCC) do całej bazy; zwróć best + top-3.
- Baza: ikony wszystkich `buyable()` itemów pobrane z `base + item.image`
  (webp; dekoduje OpenCV imgcodecs), zapisane w katalogu cache z manifestem
  `id → plik`; pobieranie z pauzą ~50 ms; brakująca ikona ⇒ pomiń item,
  zaloguj.
- Próg pewności: `cosine ≥ 0.55` ⇒ pewne; poniżej ⇒ oznacz jako niepewne
  i pozwól warstwie wyżej użyć top-3 (tie-break: zgodność kandydata
  z `typical_build` bohatera). Próg zweryfikuj empirycznie w M4 i popraw.
- `looks_empty`: puste sloty są ciemne/jednolite — heurystyka na średniej
  jasności i stddev ROI; progi wyznaczysz na realnych zrzutach (M4).

### 6.7 `vision/capture` (Windows-only)

- DXGI Desktop Duplication (D3D11): duplikacja pulpitu → `cv::Mat` BGRA→BGR.
- Interfejs `ICapture { cv::Mat grab(); }` + implementacja `DxgiCapture`;
  reszta systemu zna tylko interfejs (testowalność: `FileCapture` czyta PNG).
- Tryb gry: borderless/windowed działa zawsze; fullscreen exclusive może
  nie być duplikowalny — udokumentuj w README, nie obchodź.
- Wydajność: odczyt na żądanie (hotkey) lub polling ≤ 2 Hz. Nie budować
  toru 60 fps.

### 6.8 `vision/calibration`

- Plik `calibration.json` per rozdzielczość, schemat (elastyczny, zaprojektuj
  finalnie sam, ale w tym duchu):

```json
{ "resolution": [2560, 1440],
  "enemy_item_grid": { "origin": [x, y], "slot": [w, h],
                        "dx": px_między_slotami, "dy": px_między_wierszami,
                        "cols": 6, "rows": 5 } }
```

- Komenda `predeye calibrate`: zrób zrzut (klawisz po odliczaniu, użytkownik
  trzyma TAB), zapisz PNG; następnie tryb podglądu: wczytaj PNG + JSON,
  narysuj siatkę na obrazie i zapisz `preview.png` — użytkownik iteruje
  wartości w JSON aż siatka trafia w sloty. Bez GUI, bez dodatkowych zależności.

### 6.9 `vision/scoreboard_reader`

- Wejście: klatka + kalibracja → wytnij `rows × cols` ROI → `looks_empty`?
  → `IconMatcher::match` → per wiersz (wróg) lista `Item`.
- Wyjście: `std::vector<std::vector<Item>>` + metadane pewności.
- Dalej: `classify_items` per wróg → `refine_enemy_profile` → `counter_build`.

### 6.10 `app/` — CLI

```
predeye build   "<hero>" <rola>                      # build pod cel
predeye counter "<hero>" <rola> <wróg1> ... <wróg5>  # counter z typowych buildów
predeye live    "<hero>" <rola>                      # M5: pętla z odczytem ekranu
predeye calibrate                                    # M3: kalibracja siatki
predeye fetch-icons                                  # M2: pobranie bazy ikon
```

- `live`: pętla; hotkey **F9** (`GetAsyncKeyState`) = „odczytaj scoreboard
  teraz" (użytkownik trzyma TAB); po odczycie wypisz: rozpoznane itemy per
  wróg (+ pewność), doostrzony profil, świeży counter-build oraz **diff**
  względem poprzedniego odczytu („Wraith: +Tainted Rounds"). `Ctrl+C` kończy.
- Wyjście: konsola, czysty tekst. Okno/TTS = M6 (pytaj przed).
- Nazwy bohaterów z argumentów matchuj case-insensitive po display_name
  i slug; nieznana nazwa ⇒ czytelny błąd z podpowiedzią najbliższych nazw.

---

## 7. Struktura repo i toolchain

```
predeye/
  CMakeLists.txt          # C++17, warnings-as-errors nie; ale /W4 czysto
  vcpkg.json
  core/                   # przenośne: models, omeda_client, hero_context,
                          #            enemy_build, build_engine
  vision/                 # icon_matcher (przenośny), capture_dxgi (WIN32),
                          #   calibration, scoreboard_reader
  app/                    # main.cpp (CLI)
  tools/                  # icon_harness (walidacja NCC), pomocnicze
  tests/                  # doctest + fixtures/
  reference/              # (opcjonalnie) pliki prototypu
  assets/                 # cache ikon (gitignore)
  docs/
```

- **Toolchain:** Windows 10/11, MSVC (VS Build Tools), CMake ≥ 3.21, vcpkg
  (manifest mode). Rdzeń `core/` + `icon_matcher` muszą się kompilować też
  bez WIN32 (guardy CMake `if(WIN32)` na capture/hotkey).
- **Zależności (zamknięta lista):** `curl`, `nlohmann-json`,
  `opencv4` (potrzebne: core, imgproc, imgcodecs z PNG i **WebP**; przytnij
  features w manifeście), `doctest` (testy). Nic więcej bez pytania.
  Tesseract/OCR: NIE w tym zakresie (dopiero M7).
- Build: `cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`
  → `cmake --build build --config Release`. Dodaj `docs/BUILD.md` z krokami.

---

## 8. Milestones (kolejność wiążąca, commit per milestone)

- **M0 — szkielet:** struktura repo, CMake+vcpkg, pusty CLI się buduje na
  MSVC /W4 czysto. DONE = zielony build + `predeye --help`.
- **M1 — rdzeń:** moduły §6.1–6.5 + komendy `build`/`counter`; testy
  parserów i silnika na fixtures zielone; smoke na żywym API ręcznie.
  DONE = `predeye counter "The Fey" midlane Wraith Wukong GRIM.exe Kallari Zinx`
  drukuje sensowny build z AntiHeal (sanity §9).
- **M2 — matcher offline:** `fetch-icons`, `IconMatcher`, harness walidacyjny
  (§9). DONE = progi jakości osiągnięte na pełnej bazie ikon.
- **M3 — capture + kalibracja:** `DxgiCapture`, `calibrate` z preview.
  DONE = użytkownik potwierdza siatkę trafiającą w sloty na swoim zrzucie.
- **M4 — scoreboard end-to-end:** `scoreboard_reader` na realnych zrzutach
  dostarczonych przez użytkownika; strojenie `looks_empty` i progu cosine.
  DONE = ≥ 90% poprawnie rozpoznanych niepustych slotów na dostarczonym
  zestawie zrzutów; niepewne sloty sensownie raportowane.
- **M5 — tryb live:** pętla F9 + diff + counter na żywo. DONE = pełny cykl
  na działającej grze u użytkownika.
- **M6 — opcjonalne (KAŻDE wymaga zgody):** małe okno always-on-top lub TTS;
  EHP w funkcji oceny; kolejność zakupów z `build_paths`; wagi z regresji na
  danych meczowych; coach LLM przez lokalną Ollamę (payload = wyłącznie
  policzone liczby, zakaz zmyślania).
- **M7 — opcjonalne:** OCR HUD (Tesseract), minimapa.

---

## 9. Testy i kryteria akceptacji

- Framework: doctest, uruchamiane przez CTest. Testy NIE dotykają sieci —
  fixtures w `tests/fixtures/` (zapisz raz: pełne `items.json`, `heroes.json`,
  próbka `builds.json` dla 2–3 bohaterów).
- **Sanity silnika (fixture-based, wagi mid-maga {mp 1.0, pen 2.5, haste 0.6}):**

| item | cena | mp | pen | score | eff (pkt/1000 g) |
|---|---|---|---|---|---|
| Wraith Leggings | 3100 | 100 | 11 | 127.5 | 41.13 |
| Amulet Of Chaos | 3000 | 95 | 11 | 122.5 | 40.83 |
| Diffusal Cane | 1200 | 35 | 5 | 47.5 | 39.58 |
| Scepter | 850 | 30 | 0 | 30.0 | 35.29 |

  Wagi countera (pen→3.1, arF 0.6, arM 0.2): Wraith Leggings → 134.1;
  Golem's Gift (mp 75, ha 15, arF 40) 84.0 → 108.0. Toleruj różnice, jeśli
  fixture zostanie odświeżony po patchu — wtedy przelicz wartości oczekiwane.
- **Golden counter (fixture):** dla (The Fey, mid, wrogowie z heal+crit+tank)
  wynik zawiera ≥1 item `AntiHeal` i ≥1 `AntiTank`, koszt ≤ budżetu, ≤1 Crest,
  brak duplikatów.
- **Harness ikon (`tools/icon_harness`):** augmentacje: resize 32–54 px,
  kontrast ×0.85–1.2, jasność ±25, szum gaussowski σ 4–12, jitter ±2 px;
  ≥5 prób/ikonę na pełnej bazie. Progi WIĄŻĄCE: top-1 ≥ 99% (reżim
  realistyczny), ≥ 95% (pesymistyczny), top-3 = 100%.
- Testy parserów: pułapki z §5 (skale winrate, Unknown, id 75, null-e).

---

## 10. Styl kodu

- C++17. `#pragma once`. Wyjątki dozwolone (jak w prototypie), RAII.
- Komentarze po polsku, zwięzłe, wyjaśniające DLACZEGO; identyfikatory EN.
- Formatowanie: clang-format (dodaj `.clang-format`, styl zbliżony do LLVM,
  4 spacje, kolumna 100).
- Bez globali poza stałymi konfiguracyjnymi; moduły komunikują się przez
  jawne struktury z §6.
- Liczby prezentowane użytkownikowi zaokrąglaj świadomie (ceny int, oceny 1
  miejsce po przecinku).

---

## 11. Współpraca z właścicielem

- **Pytaj, gdy:** spec jest niejednoznaczna, chcesz odstąpić od §3/§6/§7,
  wchodzisz w M6/M7, wynik pomiaru przeczy założeniu. Pytaj konkretnie,
  z propozycją rekomendowanej opcji.
- **Nie pytaj o:** rzeczy rozstrzygnięte jako WIĄŻĄCE; drobiazgi
  implementacyjne mieszczące się w spec.
- Commituj małymi krokami; opis commita mówi CO i PO CO.
- Po każdym milestone: krótkie podsumowanie (co działa, jak uruchomić,
  co następne) w `docs/PROGRESS.md`.

## 12. Poza zakresem — na stałe

Automatyzacja rozgrywki, wysyłanie inputu do gry, czytanie/modyfikacja
pamięci procesu, wstrzykiwanie DLL, obchodzenie anti-cheata, trening sieci
neuronowych do rozpoznawania itemów (niepotrzebny — metoda NCC zmierzona),
publiczna dystrybucja.

## 13. Pytania otwarte — zadaj użytkownikowi przy starcie

1. W jakiej rozdzielczości i trybie okna (borderless?) chodzi gra?
2. Czy dostarczysz 3–5 zrzutów ekranu scoreboardu (TAB) przed M3/M4?
   (Potrzebne do kalibracji i strojenia progów.)
3. Czy katalog `reference/` z plikami prototypu będzie w repo?
4. Preferencje na M6 (okno vs TTS vs nic; Ollama tak/nie) — dopiero gdy
   M5 działa.
