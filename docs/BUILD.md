# Budowanie predeye

Projekt buduje dwa pliki wykonywalne:

- `predeye` — narzędzie wiersza poleceń (build/counter/calibrate/fetch-icons/live),
- `predeye-gui` — graficzna powłoka (Dear ImGui + GLFW).
  GUI wymaga toru wizyjnego (`PREDEYE_VISION=ON`).

**Toolchain: wyłącznie GCC.** Na Windows przez **MSYS2/MinGW-w64 (UCRT64)**,
na Linuksie systemowy g++. MSVC nie jest wspierane (CMake przerwie konfigurację
z czytelnym komunikatem).

## Windows — MSYS2/UCRT64 (zalecana droga)

1. Zainstaluj [MSYS2](https://www.msys2.org/) (domyślnie `C:\msys64`).
2. Otwórz konsolę **MSYS2 UCRT64** (nie MSYS ani MINGW64) i doinstaluj toolchain
   oraz zależności:

```sh
pacman -S --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-opencv \
    mingw-w64-ucrt-x86_64-curl \
    mingw-w64-ucrt-x86_64-nlohmann-json \
    mingw-w64-ucrt-x86_64-doctest \
    mingw-w64-ucrt-x86_64-glfw \
    git
```

3. Budowanie (w konsoli UCRT64, w katalogu repo):

```sh
cmake --preset msys2-ucrt64
cmake --build build -j
```

Pliki: `build/gui/predeye-gui.exe`, `build/app/predeye.exe`. Uruchamiaj je
z konsoli UCRT64 (albo dodaj `C:\msys64\ucrt64\bin` do PATH — tam są DLL-e
OpenCV/curl/glfw).

### CLion na Windows

W *Settings → Build, Execution, Deployment → Toolchains* dodaj toolchain
**MinGW** i wskaż `C:\msys64\ucrt64` jako środowisko (CLion sam znajdzie
gcc/g++/gdb). Potem otwórz projekt — CLion wykryje presety CMake; wybierz
**msys2-ucrt64** i cel `predeye-gui`.

## Linux

```sh
sudo apt install cmake g++ ninja-build git libcurl4-openssl-dev \
     nlohmann-json3-dev doctest-dev libglfw3-dev libgl1-mesa-dev libopencv-dev
cmake --preset default
cmake --build build -j
```

Dodaj `-DPREDEYE_VISION=OFF`, by pominąć OpenCV/tor wizyjny (buduje się tylko
rdzeń + CLI build/counter; GUI wymaga wizji).

## Wariant vcpkg (opcjonalny)

Gdy nie chcesz pakietów systemowych, preset `vcpkg` klonuje i bootstrapuje
vcpkg do `./.vcpkg` i buduje zależności ze źródeł (pierwszy raz bardzo długo —
OpenCV):

```sh
cmake --preset vcpkg
cmake --build build-vcpkg -j
```

Własne vcpkg: ustaw `VCPKG_ROOT` — zostanie użyte zamiast auto-klonowania.
Na Windows z MinGW pamiętaj o tripletach:
`-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic`.
Nieudany bootstrap vcpkg nie przerywa konfiguracji — CMake spada na biblioteki
systemowe z ostrzeżeniem.

## Opcje CMake

| Opcja | Domyślnie | Znaczenie |
|---|---|---|
| `PREDEYE_BUILD_GUI` | ON | buduje `predeye-gui` (ImGui + GLFW); wymaga `PREDEYE_VISION=ON` |
| `PREDEYE_BUILD_CLI` | ON | buduje `predeye` (CLI) |
| `PREDEYE_VISION` | ON | tor wizyjny (OpenCV): capture, matcher, kalibracja |
| `PREDEYE_AUTO_VCPKG` | OFF | auto-klonowanie vcpkg, gdy brak toolchaina |

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

## Uwagi

- Gra w trybie fullscreen exclusive może nie być duplikowalna przez DXGI —
  używaj borderless/windowed (udokumentowane, nie obchodzimy).
- Cache API: `%LOCALAPPDATA%/predeye/cache` (Windows), `~/.cache/predeye` (inne).
- GUI wykonuje wywołania API w wątku tła; przy braku sieci pokazuje błąd w
  pasku statusu i pozwala wpisać nazwę bohatera ręcznie (bez listy wyboru).
