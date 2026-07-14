cmake_minimum_required(VERSION 3.19)

# Auto-vcpkg PRZED project() — zapewnia toolchain "za pierwszym razem"
# (np. otwierajac w CLion). Szczegoly i wylaczenie: cmake/AutoVcpkg.cmake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/AutoVcpkg.cmake)

project(predeye VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opcje budowania (patrz docs/BUILD.md).
option(PREDEYE_VISION "Tor wizyjny (OpenCV): capture, matcher, kalibracja" ON)
option(PREDEYE_BUILD_GUI "Graficzna powloka (Dear ImGui + GLFW)" ON)
option(PREDEYE_BUILD_CLI "Narzedzie wiersza polecen predeye" ON)

# Projekt celuje wylacznie w GCC (Linux oraz MSYS2/MinGW-w64 na Windows) —
# patrz docs/BUILD.md. Wysoki poziom ostrzezen: ostrzezenie = do naprawy
# przed commitem (§3.4).
if(MSVC)
    message(FATAL_ERROR
        "predeye nie wspiera juz MSVC — buduj GCC-iem. Na Windows uzyj "
        "MSYS2/MinGW-w64 (srodowisko UCRT64), kroki w docs/BUILD.md.")
endif()
add_compile_options(-Wall -Wextra)

# windows.h (wciagany przez d3d11.h w capture_dxgi i wprost w app/main.cpp)
# definiuje makra min/max, ktore rozbijaja std::min/std::max w naglowkach
# OpenCV (lawina bledow skladni w imgproc.hpp). NOMINMAX je wylacza;
# WIN32_LEAN_AND_MEAN przycina windows.h do potrzebnego minimum.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(core)

if(PREDEYE_VISION)
    # find_package w scope roota — cele importowane OpenCV (opencv_core/imgproc/
    # imgcodecs) sa widoczne tylko w katalogu, ktory je stworzyl, oraz w jego
    # podkatalogach. Wywolanie tutaj, PRZED add_subdirectory, udostepnia je
    # wszystkim modulom (vision/tools/app/gui/tests). Bez tego CMake traktuje
    # np. "opencv_imgproc" jak surowa nazwe biblioteki i przekazuje linkerowi
    # -lopencv_imgproc → blad "cannot find -lopencv_imgproc".
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
    add_subdirectory(vision)
    add_subdirectory(tools)
endif()

if(PREDEYE_BUILD_CLI)
    add_subdirectory(app)
endif()

if(PREDEYE_BUILD_GUI)
    # GUI to wylacznie zakladki Kalibracja i Live (build/counter zostaly
    # w CLI) — bez toru wizyjnego nie mialoby zadnej tresci.
    if(PREDEYE_VISION)
        add_subdirectory(third_party/imgui)
        add_subdirectory(gui)
    else()
        message(WARNING "predeye-gui wymaga PREDEYE_VISION=ON "
                        "(zakladki Kalibracja/Live) — pomijam GUI.")
    endif()
endif()

enable_testing()
add_subdirectory(tests)
