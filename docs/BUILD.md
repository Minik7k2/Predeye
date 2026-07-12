# Budowanie predeye

Projekt buduje dwa pliki wykonywalne:

- `predeye` — narzędzie wiersza poleceń (build/counter/calibrate/fetch-icons),
- `predeye-gui` — graficzna powłoka (Dear ImGui + GLFW): zakładki **Build** i
  **Counter**, wywołania API w tle.

## CLion — uruchomienie „za pierwszym razem"

Repo zawiera `CMakePresets.json`, więc CLion sam wykryje profile. Toolchain
vcpkg konfiguruje się **automatycznie** (`cmake/AutoVcpkg.cmake`) — nie trzeba
niczego ustawiać ręcznie.

1. **File → Open** i wskaż katalog repo.
2. CLion pokaże presety CMake — wybierz jeden:
   - **predeye (pełny)** — GUI + CLI + tor wizyjny (OpenCV). Pierwsza
     konfiguracja jest długa: vcpkg buduje OpenCV ze źródeł.
   - **predeye (szybki)** — GUI + CLI **bez** OpenCV. Znacznie szybszy pierwszy
     build; brak komend wizyjnych (`fetch-icons`, `calibrate`, `live`).
3. Poczekaj, aż CMake skończy konfigurację (za pierwszym razem klonuje i
   bootstrapuje vcpkg do `./.vcpkg`, potem pobiera zależności).
4. Wybierz cel **predeye-gui** (lub **predeye**) z listy konfiguracji
   uruchomieniowych u góry i kliknij **Run** (zielona strzałka).

Jeśli masz już własne vcpkg, ustaw zmienną środowiskową `VCPKG_ROOT` — zostanie
użyta zamiast auto-klonowania. Wbudowana integracja vcpkg w CLion (Settings →
Build → vcpkg) też zadziała: gdy poda własny `CMAKE_TOOLCHAIN_FILE`,
`AutoVcpkg.cmake` go uszanuje.

## Wiersz poleceń (Windows/Linux/macOS)

Wymagania: CMake ≥ 3.21, kompilator C++17 (MSVC / GCC / Clang), git.

```sh
# Pełny build (auto-vcpkg — pierwszy raz długi przez OpenCV):
cmake --preset default
cmake --build build --config Release

# Szybki build bez OpenCV:
cmake --preset gui-only
cmake --build build-gui --config Release
```

Pliki: `build/gui/predeye-gui`, `build/app/predeye` (na Windows w podkatalogu
`Release/`).

### Bez vcpkg (biblioteki systemowe)

Na Linuksie można użyć bibliotek z menedżera pakietów zamiast vcpkg:

```sh
sudo apt install cmake g++ git libcurl4-openssl-dev nlohmann-json3-dev \
     doctest-dev libglfw3-dev libgl1-mesa-dev libopencv-dev
cmake -B build -S . -DPREDEYE_AUTO_VCPKG=OFF
cmake --build build -j
```

Dodaj `-DPREDEYE_VISION=OFF`, by pominąć OpenCV/tor wizyjny.

## Opcje CMake

| Opcja | Domyślnie | Znaczenie |
|---|---|---|
| `PREDEYE_BUILD_GUI` | ON | buduje `predeye-gui` (ImGui + GLFW) |
| `PREDEYE_BUILD_CLI` | ON | buduje `predeye` (CLI) |
| `PREDEYE_VISION` | ON | tor wizyjny (OpenCV): capture, matcher, kalibracja |
| `PREDEYE_AUTO_VCPKG` | ON | auto-klonowanie vcpkg, gdy brak toolchaina |

Przy `PREDEYE_VISION=OFF` w trybie vcpkg dodatkowo warto wyłączyć domyślną
funkcję manifestu (`-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON`), by vcpkg nie
instalował OpenCV — robi to preset **gui-only**.

## Zależności

Zamknięta lista (§7 + graficzna powłoka): `curl`, `nlohmann-json`,
`glfw3`, `doctest`, `opencv4` (tylko przy `PREDEYE_VISION=ON`; core/imgproc/
imgcodecs + PNG + WebP). Dear ImGui jest wersjonowane w repo
(`third_party/imgui`) — nie pobiera się przez menedżer pakietów.

## Testy

```sh
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Testy nie dotykają sieci — chodzą na fixtures w `tests/fixtures/`.

## Toolchain na Windows: MSVC vs MinGW (ważne dla CLion)

Projekt celuje w **MSVC** (CLAUDE.md §7), ale działa też pod **MinGW** (domyślny
toolchain CLion). Sedno problemu: vcpkg na Windows domyślnie instaluje biblioteki
dla tripletu `x64-windows` (zbudowane MSVC), a MinGW nie zlinkuje bibliotek C++
z MSVC — link `predeye-gui` kończyłby się setkami `undefined reference to
cv::...` (inne dekoracje nazw symboli C++).

**To rozwiązuje się samo.** `cmake/AutoVcpkg.cmake` wykrywa kompilator **przed**
`project()` i gdy widzi MinGW-a (ścieżka `gcc`/`g++`, ID `GNU`, generator
„MinGW Makefiles"), automatycznie ustawia triplet vcpkg na `x64-mingw-dynamic`
(oraz host triplet). MSVC (`cl.exe`) zostaje przy `x64-windows`. Nie trzeba nic
dopisywać do opcji CMake — wystarczy wybrać toolchain w CLion i konfigurować.

Przy pierwszej konfiguracji pod MinGW vcpkg przebuduje OpenCV/curl/glfw ze źródeł
pod ten toolchain (triplet „community", długo). Jeśli wcześniej konfiguracja padła
na starym toolchainie, **usuń katalog `cmake-build-*` / `build*`** przed ponowną
próbą — inaczej stary cache może trzymać niedopasowane biblioteki.

Chcesz jawnie wymusić wybór? Podaj własny triplet w opcjach CMake profilu, np.
`-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic`
(auto-detekcja szanuje ręcznie ustawiony `VCPKG_TARGET_TRIPLET`). Straznik w
`CMakeLists.txt` nadal łapie ewentualne ręczne wymuszenie sprzecznej pary
(MinGW + triplet MSVC) i przerywa z czytelnym komunikatem.

## Uwagi

- Gra w trybie fullscreen exclusive może nie być duplikowalna przez DXGI —
  używaj borderless/windowed (udokumentowane, nie obchodzimy).
- Cache API: `%LOCALAPPDATA%/predeye/cache` (Windows), `~/.cache/predeye` (inne).
- GUI wykonuje wywołania API w wątku tła; przy braku sieci pokazuje błąd w
  pasku statusu i pozwala wpisać nazwę bohatera ręcznie (bez listy wyboru).
