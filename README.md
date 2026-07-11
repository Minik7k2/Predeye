# predeye

Prywatny asystent-trener do gry **Predecessor** (MOBA). Działa **obok** gry —
doradza, niczego nie automatyzuje.

Trzy filary:

1. **Mózg buildów** — z publicznego API Omeda.city generuje build pod cel
   (bohater + rola) i counter-build pod konkretny skład wroga.
2. **Oczy** — odczyt realnych itemów wroga ze scoreboardu (TAB) przez zrzut
   ekranu (DXGI Desktop Duplication) + dopasowanie ikon metodą kolor-NCC 32×32.
3. **Porady** — tryb `live`: po każdym odczycie scoreboardu świeży
   counter-build i diff względem poprzedniego odczytu, w konsoli.

Czym projekt **nie jest**: botem, makrem, cheatem. Nie wysyła żadnego wejścia
do gry, nie czyta pamięci procesu, nie wstrzykuje DLL. Jedyne wejście wizualne
to zrzut ekranu przez publiczne API systemu.

## Użycie

Dwa pliki wykonywalne:

- **`predeye-gui`** — graficzna powłoka (zakładki *Build* i *Counter*).
  W CLion: otwórz projekt, wybierz preset CMake, uruchom cel `predeye-gui`.
- **`predeye`** — wiersz poleceń:

```
predeye build   "<bohater>" <rola>                      # build pod cel
predeye counter "<bohater>" <rola> <wróg1> ... <wróg5>  # counter z typowych buildów
predeye fetch-icons                                     # pobranie bazy ikon
predeye calibrate                                       # kalibracja siatki (M3)
predeye live    "<bohater>" <rola>                      # tryb live (M5)
```

Tryb `live` wymaga wcześniejszego `fetch-icons` i `calibrate` (plik
`calibration.json`). Na Windows odczyt wyzwala hotkey **F9** (trzymaj TAB);
`predeye live "<bohater>" <rola> --image <png>` robi pojedynczy odczyt z pliku
(przydatne do testu na dowolnym systemie).

Budowanie i uruchomienie w CLion „za pierwszym razem": `docs/BUILD.md`.
Postęp prac: `docs/PROGRESS.md`. Pełna specyfikacja projektu: `CLAUDE.md`.

Uwaga: tryb fullscreen exclusive może nie być duplikowalny przez DXGI —
zalecany tryb okna to borderless/windowed.
