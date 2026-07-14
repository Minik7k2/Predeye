# predeye

Prywatny **asystent rankedowy** do gry **Predecessor** (MOBA). Działa **obok**
gry — doradza, niczego nie automatyzuje.

Filary:

1. **Mózg buildów** — z publicznego API Omeda.city generuje build pod cel
   (bohater + rola) i counter-build pod konkretny skład wroga.
2. **Oczy** — odczyt ekranu przez zrzut (DXGI Desktop Duplication)
   + dopasowanie ikon metodą kolor-NCC 32×32: itemy i **portrety bohaterów**
   ze scoreboardu (TAB) oraz bany/picki z ekranu draftu.
3. **Kreator rankedowy** (`predeye-gui`) — prowadzi przez etapy meczu:
   **bany → wybór bohatera → eternals + skill → crest → itemy (live)**.
   Sugestie banów/picków (meta + własna baza kontr), crest i skill order
   z buildów społeczności, eternalsy z własnej bazy, a w meczu
   **„następny zakup + dlaczego"** po polsku.

Czym projekt **nie jest**: botem, makrem, cheatem. Nie wysyła żadnego wejścia
do gry, nie czyta pamięci procesu, nie wstrzykuje DLL. Jedyne wejście wizualne
to zrzut ekranu przez publiczne API systemu.

## Użycie

Dwa pliki wykonywalne:

- **`predeye-gui`** — kreator rankedowy (etapy po lewej, wspólne ustawienia
  w nagłówku, `Kalibracja` jako tryb zaawansowany). Hotkey **F9** (Windows)
  wyzwala odczyt ekranu w bieżącym etapie. Kalibracja siatek scoreboardu
  robi się **automatycznie** przy pierwszym odczycie live (przy widocznych
  itemach); regiony draftu wymagają jednorazowego ustawienia na zrzucie
  ekranu draftu.
- **`predeye`** — wiersz poleceń:

```
predeye build   "<bohater>" <rola>                      # build pod cel
predeye counter "<bohater>" <rola> <wróg1> ... <wróg5>  # counter z typowych buildów
predeye fetch-icons                                     # pobranie bazy ikon
predeye calibrate                                       # kalibracja siatki (CLI)
predeye live    "<bohater>" <rola>                      # tryb live w konsoli
```

Tryb `live` w CLI wymaga `fetch-icons` i pliku `calibration.json`. Na Windows
odczyt wyzwala hotkey **F9** (trzymaj TAB); `--image <png>` robi pojedynczy
odczyt z pliku (test na dowolnym systemie).

## Dane własne (katalog `data/`)

Kreator opiera się na ręcznie utrzymywanych plikach w repo:

| Plik | Zawartość |
|---|---|
| `data/hero_pool.json` | Twoja pula bohaterów + główna rola (sugestie picków) |
| `data/counters.json` | baza kontr (kto kogo wygrywa) z notatkami PL |
| `data/eternals.json` | eternalsy/perki: opisy PL, ikony, rekomendacje per bohater/rola |
| `data/pl_items.json` | spolszczenie wszystkich kupowalnych itemów (co robią i jak); regeneracja po patchu: `tools/gen_pl_items.py` |

API Omeda.city nie zna eternalsów ani kontr — im pełniejsze te pliki, tym
trafniejsze porady.

Budowanie (GCC: Linux / MSYS2-UCRT64 na Windows): `docs/BUILD.md`.
Postęp prac: `docs/PROGRESS.md`. Pełna specyfikacja projektu: `CLAUDE.md`.

Uwaga: tryb fullscreen exclusive może nie być duplikowalny przez DXGI —
zalecany tryb okna to borderless/windowed.
