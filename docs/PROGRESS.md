# Postęp prac

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
