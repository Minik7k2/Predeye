# Budowanie predeye

## CLion (zalecane na Windows)

Repo zawiera `CMakePresets.json` — CLion wykrywa presety automatycznie.

1. **Wymagania jednorazowe:**
   - Visual Studio Build Tools 2022 (workload „Desktop development with C++")
     — CLion znajdzie toolchain MSVC sam.
   - vcpkg:
     ```bat
     git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
     %USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
     ```
   - Zmienna środowiskowa `VCPKG_ROOT=%USERPROFILE%\vcpkg` (Ustawienia
     systemowe → Zmienne środowiskowe; po dodaniu **zrestartuj CLion**).
2. **Otwórz katalog projektu w CLion** (File → Open → folder z repo).
3. `Settings → Build, Execution, Deployment → Toolchains`: upewnij się, że
   jest toolchain **Visual Studio** (amd64).
4. `Settings → ... → CMake`: włącz profile z presetów
   **windows-msvc-debug** i/lub **windows-msvc-release** (CLion pokaże je
   na liście „CMake Presets"). Pierwsza konfiguracja potrwa długo — vcpkg
   buduje OpenCV/curl z manifestu (nawet 20–40 min, jednorazowo).
5. **Uruchamianie:** wybierz konfigurację `predeye` i w
   `Edit Configurations...` ustaw *Program arguments*, np.:
   ```
   counter "The Fey" midlane Wraith Wukong GRIM.exe Kallari Zinx
   ```
   oraz *Working directory* na katalog repo (tam powstają
   `calibration.json` / `preview.png` przy komendzie `calibrate`).
6. **Testy:** target `predeye_tests` uruchamiany jak zwykły program albo
   przez zakładkę CTest (CLion integruje `ctest` automatycznie).

Na Linux/macOS CLion podchwyci preset **linux-release** (wymaga pakietów
systemowych — patrz niżej).

## Windows (czysta linia komend, bez CLiona)

Wymagania: Windows 10/11, Visual Studio Build Tools (MSVC), CMake >= 3.21, vcpkg.

```bat
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
build\app\Release\predeye.exe --help
```

Zależności instaluje vcpkg w trybie manifestu (`vcpkg.json`): curl, nlohmann-json,
opencv4 (core/imgproc/imgcodecs + PNG + WebP), doctest.

## Linux / macOS (rdzeń, bez capture)

Rdzeń (`core/`) i `icon_matcher` są przenośne; Windows-only jest tylko
`vision/capture` (DXGI) i hotkey trybu `live`. Na Ubuntu:

```sh
sudo apt install cmake g++ libcurl4-openssl-dev nlohmann-json3-dev doctest-dev libopencv-dev
cmake -B build -S .
cmake --build build -j
./build/app/predeye --help
```

## Testy

```sh
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Testy nie dotykają sieci — chodzą na fixtures w `tests/fixtures/`.

## Uwagi

- Gra w trybie fullscreen exclusive może nie być duplikowalna przez DXGI —
  używaj borderless/windowed (udokumentowane, nie obchodzimy).
- Cache API: `%LOCALAPPDATA%/predeye/cache` (Windows), `~/.cache/predeye` (inne).
