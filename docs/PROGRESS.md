# Postęp prac

## M4 — kalibracja zmierzona na realnych zrzutach + naprawa sygnatur ikon

- **Realne zrzuty TAB w repo**: `tests/fixtures/screens/` — 3 reprezentatywne
  (early/mid/late, 1080p, patch 5.4.4) + `calibration_1080p.json` (wzorzec);
  pozostałe 10 lokalnie w `local/` (gitignore). Źródło: zrzuty Steam
  użytkownika (2 mecze).
- **Geometria zmierzona numerycznie** (skan jasności ramek slotów, zgodna
  w obu meczach co do piksela): wnętrze slotu 52×52 (ramka 2 px, przerwa
  4 px), dx 60, dy 145, **cols 6** (nie 7 — „7. kwadrat" to crest z własną
  ikoną, oddzielony separatorem ~67 px przed item1), rows 5; origin wróg
  (1343, 250), sojusznik (333, 250). Panele **nie są lustrzane** — panel
  wroga = panel sojuszników + 1010 px w x (`mirror_grid`/`default_for`
  do poprawy — następny krok).
- **Naprawa sygnatur ikon (zmiana §6.6, zaakceptowana)**: grafiki
  `item.image` z API to duże webp z kanałem alfa, białą winietą i
  marginesami — NIE odpowiadają kafelkom na scoreboardzie; stare sygnatury
  (imread bez alfy) dawały na realnym zrzucie bzdury (17/28 niepewnych,
  identyfikacje losowe) mimo idealnej siatki. Nowe przygotowanie
  `tile_from_asset`: IMREAD_UNCHANGED → kwadratowy bbox po alfie →
  kompozycja na tle kafelka BGR(18,21,24) (zmierzonym ze zrzutów); harness
  używa tej samej ścieżki dla sond. Dodatkowo etap 2 matchera bierze 16
  kandydatów (nie 8) — 8 gubiło bliźniacze sztylety (Sai/Spell Slasher).
- **Wyniki po naprawie**: harness (10 prób/ikonę) — **100% top-1 i top-3
  w OBU reżimach** (progi wiążące 99/95/100 z zapasem). Realne zrzuty:
  mecz 1 — 28 odczytów, 0 niepewnych, identyfikacje spójne (bliźniacze
  ikony w różnych wierszach → identyczne itemy, „Codex, Codex" u gracza
  z dwoma kodeksami); mecz 2 late-game — 56 odczytów, 3 niepewne wyłącznie
  na wierszu rozłączonego gracza (przyciemniony overlay „DISCONNECTED").
- **Odkrycie (zbadane)**: omeda.city przeniósł strony HTML i `/assets/*` na
  **pred.gg** (308), ale stare JSON API działa TYLKO bezpośrednio na
  omeda.city — na pred.gg te ścieżki to 404, a nowe API pred.gg ma inny,
  niekompatybilny schemat. `kBaseUrl` ZOSTAJE na omeda.city (komentarz
  ostrzegawczy w `omeda_client.hpp`); redirect assetów załatwia
  follow-redirects. Zmiana bazy na pred.gg cicho psuła pobieranie buildów
  (items/heroes maskował 24h cache) — cofnięta po pomiarze.
- **`predeye calibrate --auto`** (`vision/auto_calibration`, przenośny):
  detekcja siatek wprost z klatki TAB — RLE jasności per linia znajduje
  okresowe łańcuchy „odcinek ramki ~52 px, przerwa 4–8 px, krok 60";
  linie grupowane po x0 w panele, pasma y sklejane w wiersze (ramka górna
  i dolna wiersza to osobne pasma odległe o ~wysokość slotu). Przerwa za
  crestem (18 px) celowo wypada poza zakres — crest zostaje poza siatką.
  Panel niewykryty wprost (jasne tło prześwituje przez półprzezroczysty
  panel i zabija ramki) jest doliczany z przesunięcia +1010 px i uczciwie
  raportowany. Na wszystkich 3 fixtures wynik zgodny ze zmierzoną
  kalibracją ≤2 px; `live` na auto-kalibracji czyta identycznie jak na
  ręcznej (28 itemów, 0 niepewnych). Testy: syntetyczny scoreboard +
  realne zrzuty. Przepływ z podglądem do potwierdzenia zostaje.
- **Wybór monitora**: `DxgiCapture(output)` — domyślnie automatycznie
  znajduje okno gry (klasa `UnrealWindow` + tytuł „Predecessor", publiczne
  API okien) i duplikuje JEGO monitor (u użytkownika gra na środkowym z 3);
  `--monitor N` w `calibrate`/`live` wymusza wyjście.
- Do zrobienia w M4: strojenie `looks_empty` (pojedyncze ciemne itemy
  bywają zjadane), przycisk „Wykryj automatycznie" w GUI (zakładka
  Kalibracja), potwierdzenie DXGI + pętli F9 na żywej grze u użytkownika.

## GUI — kalibracja i tryb live wyciągnięte do interfejsu graficznego (DONE)

- **Fasada `vision/vision_session` (`VisionSession`)**: wspólne źródło toru live
  dla CLI i GUI — trzyma dane API, bazę ikon, cel gracza i stan poprzedniego
  odczytu (do diffu). `read(klatka, kalibracja)` zwraca `LiveResult`
  (rozpoznane itemy per wróg + pewność, zmiany względem poprzedniego odczytu,
  doostrzony `EnemyProfile`, counter-build). CLI `predeye live` przepięte na tę
  fasadę — logika odczytu/diffu/countera nie jest już zduplikowana między CLI
  a GUI (jedno miejsce = jeden algorytm).
- **Zakładka „Kalibracja"** (`gui/main_gui.cpp`): wczytanie zrzutu PNG (na
  Windows też zrzut z gry przez DXGI z 3 s odliczaniem), podgląd klatki z
  narysowaną siatką slotów na teksturze OpenGL, suwaki `origin/slot/dx/dy/
  cols/rows` aktualizujące podgląd na żywo, **klik na obrazie ustawia origin**,
  wczytanie/zapis `calibration.json`, „domyślne dla rozdzielczości". Zastępuje
  ręczną iterację `calibrate` → `preview.png` z CLI (odblokowuje M3/M4 z GUI).
- **Zakładka „Live"**: wybór bohatera + roli, plik kalibracji, odczyt z gry
  (Windows, DXGI) lub z pliku PNG (dowolny system, do testów); pierwszy odczyt
  pobiera bazę ikon. Wynik: rozpoznane itemy per wróg (z „(niepewne)"), diff,
  profil wroga i świeży counter-build w tabeli. Odczyty i pobieranie idą w
  wątku tła (`AsyncTask`) — pętla renderowania nie zamarza.
- **Tor wizyjny w GUI opcjonalny**: pod `PREDEYE_VISION`. Przy OFF GUI ma tylko
  Build/Counter (rdzeń bez OpenCV) — guardy `PREDEYE_HAS_VISION` w GUI i CMake.
- Zweryfikowane na Linuksie (OpenCV 4.6, GCC 13): pełny build (core + CLI + GUI
  + wizja + testy) `-Wall -Wextra` czysto; wariant `PREDEYE_VISION=OFF` też się
  buduje; testy 27/27 (189 asercji) zielone; GUI startuje headless (Xvfb) bez
  błędów; `predeye live --image` na syntetycznym scoreboardzie z 5 realnych ikon
  rozpoznał wszystkie (0 niepewnych) i wypisał counter z AntiTank — ta sama
  ścieżka `VisionSession`, której używa zakładka Live.
- **Do potwierdzenia u użytkownika (Windows):** zrzut DXGI z GUI, klik-kalibracja
  na realnym zrzucie i odczyt live na żywej grze (strojenie liczby kolumn 6/7 i
  progów `looks_empty`/cosine — nadal w kodzie na sztywno).

## M4/M5 — scoreboard end-to-end + tryb live (kod gotowy; pętla F9 czeka na Windows)

- **`vision/scoreboard_reader`**: `read_scoreboard(klatka, kalibracja, matcher,
  index)` — tnie `rows × cols` ROI wg siatki, odrzuca puste (`looks_empty`),
  dopasowuje ikony (`IconMatcher::match`) i zwraca `EnemyRead` per wróg
  (itemy do klasyfikacji + `SlotRead` z pewnością per slot). ROI wychodzące
  poza klatkę (zła kalibracja, nadmiarowa 7. kolumna) są traktowane jak puste —
  funkcja nigdy nie rzuca z powodu geometrii. Moduł przenośny (bez WIN32).
- **`predeye live "<bohater>" <rola>`** (`app/main.cpp`): spina odczyt →
  `classify_items`/`refine_enemy_profile` → `counter_build` → wydruk z **diffem**
  względem poprzedniego odczytu. Na Windows pętla z hotkeyem **F9**
  (`GetAsyncKeyState`, użytkownik trzyma TAB; Ctrl+C kończy); `--image <png>`
  robi pojedynczy odczyt (test na dowolnym systemie). Wymaga `calibration.json`
  (inaczej podpowiada `predeye calibrate`) i bazy ikon (`fetch-icons`).
- **Świadome uproszczenie:** tie-break niepewnych po `typical_build` bohatera
  pominięty — wymaga tożsamości bohatera per wiersz (OCR = M7, poza zakresem);
  zamiast tego pewność raportowana per slot („(niepewne)").
- Zweryfikowane e2e na Linuksie (biblioteki systemowe, OpenCV 4.6): pełna baza
  213 ikon; syntetyczny scoreboard z 5 realnych ikon → `live --image` rozpoznał
  wszystkie (0 niepewnych), doostrzył profil i wypisał counter różny od zwykłego
  buildu (waga pancerza od składu wroga). Testy `test_scoreboard_reader`
  (rozpoznanie + puste sloty + ROI poza klatką) zielone.
- **Do potwierdzenia u użytkownika (Windows):** pętla F9 na żywej grze,
  strojenie progu `looks_empty`/cosine na realnych slotach oraz liczby kolumn
  (spec 6, realne UI 7) — DONE M4/M5 = pełny cykl na działającej grze.

## GUI — graficzna powłoka + uruchomienie w CLion (DONE)

- **`gui/` (`predeye-gui`)**: powłoka na Dear ImGui + GLFW + OpenGL3. Zakładki
  **Build** (bohater + rola) i **Counter** (bohater + rola + do 5 wrogów),
  tabela wyników (item / cena / ocena / slot / powód), profil składu wroga.
- Wywołania API idą w wątku tła (`gui/async_task.hpp`) — pętla renderowania
  nie zamarza podczas pobierania danych. Lista bohaterów ładowana na starcie
  do filtrowalnych list wyboru; przy braku sieci błąd trafia do paska statusu,
  a nazwę można wpisać ręcznie.
- **Rdzeń współdzielony**: nowa fasada `core/advisor` (`Advisor::build` /
  `Advisor::counter`) zwraca gotowe struktury — używa jej GUI; CLI zachowuje
  dotychczasowe wyjście tekstowe. Rdzeń dalej bez OpenCV (przenośny).
- **Dear ImGui wersjonowane w repo** (`third_party/imgui`, v1.91.6) — brak
  dodatkowej zależności pobieranej przez menedżer pakietów; nowe zależności to
  tylko `glfw3` (+ systemowy OpenGL).
- **Uruchomienie „za pierwszym razem" w CLion**: `CMakePresets.json` (profile
  *pełny* i *szybki bez OpenCV*) + `cmake/AutoVcpkg.cmake`, który sam
  konfiguruje toolchain vcpkg (użyje `VCPKG_ROOT`/`CMAKE_TOOLCHAIN_FILE`, a gdy
  ich brak — klonuje i bootstrapuje vcpkg do `./.vcpkg`). Kroki: `docs/BUILD.md`.
- **Tor wizyjny opcjonalny**: opcja CMake `PREDEYE_VISION` (domyślnie ON);
  przy OFF budują się tylko `core` + `gui` + CLI (build/counter) bez OpenCV —
  szybki pierwszy build. CLI/testy strojone guardami `PREDEYE_HAS_VISION`.
- Zweryfikowane: pełny build (core + CLI + GUI + testy) na GCC 13 z bibliotekami
  systemowymi, `-Wall -Wextra` czysto; testy 18/18 (124 asercji) zielone; GUI
  otwiera okno i renderuje interfejs (zrzut headless pod Xvfb).

## M3 — capture + kalibracja (kod gotowy; czeka na potwierdzenie użytkownika)

- `vision/capture`: interfejs `ICapture` + `FileCapture` (testowalność bez gry).
- `vision/capture_dxgi` (WIN32): DXGI Desktop Duplication → BGR `cv::Mat`;
  retry na `WAIT_TIMEOUT`, odtworzenie duplikacji po `ACCESS_LOST`, czytelny
  komunikat przy fullscreen exclusive. **Uwaga:** pisane na Linuksie pod
  guardem `_WIN32` — pierwsza kompilacja MSVC może wymagać drobnych poprawek.
- `vision/calibration`: `calibration.json` per rozdzielczość (schemat wg §6.8),
  walidacja z czytelnymi błędami, `default_for()` skalowane z bazy 1080p,
  `draw_grid()` (ramki slotów + numery wierszy + krzyżyk na origin).
- `predeye calibrate [--image <png>] [--config <json>]`: na Windows robi zrzut
  po odliczaniu (Enter → 5 s → grab, użytkownik trzyma TAB) i zapisuje
  `calibration_shot.png`; z `--image` działa na dowolnym systemie. Tworzy
  startowy `calibration.json`, rysuje siatkę do `preview.png`; iteracja aż
  siatka trafi w sloty. Ostrzega przy niezgodności rozdzielczości.
- Zweryfikowane e2e na syntetycznym scoreboardzie 1920×1080 (5×6 slotów):
  po wpisaniu wartości siatki do JSON ramki trafiają w sloty co do piksela.
  Testy kalibracji (round-trip, geometria, podgląd) zielone.
- Gra użytkownika: **1920×1080 borderless** (§13.1 — odpowiedziane).
- Użytkownik dostarczył zrzuty w czacie (7 × scoreboard TAB, patch 5.4.4);
  na ich podstawie oszacowano wzrokowo wartości startowe `default_for()`:
  origin ~(1346, 256), slot ~46 px, dx ~59, dy ~145, rows 5, **cols 7**
  (spec zakładał 6 — na realnym UI widać 7 kwadratów; do potwierdzenia).
  Pliki PNG trafią do repo później (użytkownik na telefonie) — wtedy
  dokładne strojenie `looks_empty`/progu cosine na realnych slotach (M4).
- DONE M3 = potwierdzenie użytkownika na realnym zrzucie przez `calibrate`
  → `preview.png` u siebie.

## M2 — matcher offline (DONE)

- `vision/icon_matcher`: sygnatura kolor-NCC 32×32 (WIĄŻĄCA metoda),
  `ensure_icon_cache` + manifest `id → plik`, komenda `predeye fetch-icons`
  (pobrano pełną bazę: 213 ikon), heurystyka `looks_empty` (progi wstępne,
  strojenie w M4).
- `tools/icon_harness`: augmentacje wg §9 (resize 32–54 px, kontrast
  ×0.85–1.2, jasność ±25, szum σ 4–12, jitter ±2 px), dwa reżimy
  (realistyczny = łagodna połowa zakresów, pesymistyczny = końcówki),
  deterministyczny RNG. Waliduje realną ścieżkę `IconMatcher::match`.
- **Wyniki na pełnej bazie 213 ikon (10 prób/ikonę/reżim):**
  realistyczny top-1 100%, pesymistyczny top-1 99,9%, top-3 100% w obu —
  progi WIĄŻĄCE (99/95/100) osiągnięte.
- **Korekty wynikające z pomiarów** (czyste NCC wg §6.6 dawało na 213
  ikonach 90,3% top-1 pesymistycznie — poniżej progu; winny jitter ±2 px):
  1) łagodny low-pass (Gaussian σ1,2 po resize do 32×32) symetrycznie
     w sygnaturze bazy i próbki;
  2) dwustopniowe dopasowanie: czyste NCC wyłania top-8 kandydatów, potem
     tylko dla nich szukane jest najlepsze wyrównanie próbki po
     przesunięciach ±2 px (maksimum po całej bazie zamiast top-K psuło
     ranking — zmierzone).
  Metoda pozostaje kolor-NCC 32×32; do akceptacji właściciela.
- Testy offline matchera (syntetyczne ikony) w `tests/` — zielone.
- Następne: M3 — `DxgiCapture` + `predeye calibrate` (potrzebne odpowiedzi
  na pytania §13: rozdzielczość, tryb okna, zrzuty scoreboardu).

## M1 — rdzeń (DONE)

- Moduły `core/`: `models` (defensywny parser itemów), `omeda_client`
  (libcurl + cache dyskowy TTL + pauza ≥0,3 s), `hero_context` (profile z klas,
  presety ról, HeroDB z podpowiedziami nazw), `build_engine` (optimize /
  counter_build, dwufazowe składanie), `enemy_build` (typowe buildy z API,
  klasyfikacja itemizacji, doostrzanie profilu).
- Komendy `predeye build` i `predeye counter` działają na żywym API (smoke
  ręczny: `predeye counter "The Fey" midlane Wraith Wukong GRIM.exe Kallari
  Zinx` — drukuje counter z AntiHeal/AntiTank i ostrzeżenie o braku AntiCrit
  dla maga na stderr, zgodnie ze spec).
- Testy doctest na fixtures (`tests/fixtures/`, zapisane jednorazowo z żywego
  API): sanity silnika wg tabeli §9 (wartości zgodne 1:1 z bieżącym patchem),
  golden counter, pułapki parserów (null-e, id Yurei poza int32, total_price
  null ⇒ niekupowalny). `ctest` zielony, build bez ostrzeżeń.
- **Odstępstwo wykryte pomiarem:** `filter[current_version]=1` w
  `/builds.json` zwraca dziś pustą listę (API zmieniło się od prototypu).
  Klient próbuje z filtrem i przy pustym wyniku ponawia bez niego —
  do potwierdzenia przez właściciela.
- Następne: M2 — `fetch-icons`, `IconMatcher` (kolor-NCC 32×32), harness.

## M0 — szkielet (DONE)

- Struktura repo: `core/ vision/ app/ tools/ tests/ docs/`, CMake + vcpkg manifest,
  `.clang-format`, `.gitignore`.
- Pusty CLI (`predeye --help`) buduje się czysto na wysokim poziomie ostrzeżeń.
- Jak uruchomić: patrz `docs/BUILD.md`.
- Następne: M1 — rdzeń (models, omeda_client, hero_context, enemy_build,
  build_engine) + komendy `build`/`counter` + testy na fixtures.
