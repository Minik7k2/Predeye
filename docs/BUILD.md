# Budowanie predeye

## Windows (docelowo)

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
