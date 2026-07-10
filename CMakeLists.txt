cmake_minimum_required(VERSION 3.21)

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
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(core)

if(PREDEYE_VISION)
    add_subdirectory(vision)
    add_subdirectory(tools)
endif()

if(PREDEYE_BUILD_CLI)
    add_subdirectory(app)
endif()

if(PREDEYE_BUILD_GUI)
    add_subdirectory(third_party/imgui)
    add_subdirectory(gui)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
