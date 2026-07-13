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

# Wysoki poziom ostrzezen — ostrzezenie = do naprawy przed commitem (§3.4).
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    # std::getenv to standardowe C++ (rdzen przenosny, §4); C4996 to
    # niestandardowa deprecjacja MSVC — wyciszamy, nie przepisujemy na _dupenv_s.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# windows.h (wciagany przez d3d11.h w capture_dxgi i wprost w app/main.cpp)
# definiuje makra min/max, ktore rozbijaja std::min/std::max w naglowkach
# OpenCV (lawina bledow skladni w imgproc.hpp na MSVC). NOMINMAX je wylacza;
# WIN32_LEAN_AND_MEAN przycina windows.h do potrzebnego minimum.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Straznik niezgodnosci ABI: MinGW g++ nie zlinkuje bibliotek C++ zbudowanych
# przez MSVC. vcpkg na Windows domyslnie uzywa tripletu x64-windows (MSVC), wiec
# kompilacja MinGW-iem (np. domyslny toolchain CLion) konczy sie setkami
# "undefined reference to cv::..." — symbole C++ maja inne dekoracje nazw.
# Lapiemy to tu jednym czytelnym komunikatem zamiast lawiny bledow linkera.
# (Triplety MSVC: x64-windows, x86-windows, ...; MinGW: *-mingw-*).
if(WIN32 AND MINGW AND DEFINED VCPKG_TARGET_TRIPLET
   AND NOT VCPKG_TARGET_TRIPLET MATCHES "mingw")
    message(FATAL_ERROR
        "Niezgodnosc toolchainu: kompilujesz MinGW-iem (${CMAKE_CXX_COMPILER_ID}), "
        "ale vcpkg dostarcza biblioteki dla tripletu MSVC '${VCPKG_TARGET_TRIPLET}'. "
        "Linker MinGW nie dopasuje symboli C++ z bibliotek MSVC (undefined reference "
        "do cv::...). Wybierz jedno:\n"
        "  A) Przelacz toolchain na Visual Studio (MSVC) i usun katalog build/ "
        "(zalecane; projekt celuje w MSVC — patrz CLAUDE.md §7).\n"
        "  B) Zostan przy MinGW: skonfiguruj z "
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic "
        "i usun katalog build/ (vcpkg przebuduje zaleznosci pod MinGW).")
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
