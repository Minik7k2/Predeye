# Auto-vcpkg — zapewnia toolchain vcpkg "za pierwszym razem", bez recznej
# konfiguracji (np. otwierajac projekt w CLion). Kolejnosc wyboru:
#
#   1. CMAKE_TOOLCHAIN_FILE juz ustawiony (CLion vcpkg / preset / -D...) -> nie ruszamy.
#   2. VCPKG_ROOT w srodowisku -> uzyj tamtejszego toolchaina.
#   3. PREDEYE_AUTO_VCPKG=ON (domyslnie) -> sklonuj i zbootstrapuj vcpkg
#      lokalnie w ./.vcpkg i uzyj jego toolchaina.
#   4. W przeciwnym razie -> nic; liczymy na biblioteki systemowe (find_package).
#
# Musi byc dolaczony PRZED project() (toolchain czytany jest przy project()).

option(PREDEYE_AUTO_VCPKG "Sklonuj i zbootstrapuj vcpkg, gdy brak toolchaina" ON)

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "predeye: uzywam podanego CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    return()
endif()

if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain z VCPKG_ROOT")
    message(STATUS "predeye: toolchain z VCPKG_ROOT=$ENV{VCPKG_ROOT}")
    return()
endif()

if(NOT PREDEYE_AUTO_VCPKG)
    message(STATUS "predeye: PREDEYE_AUTO_VCPKG=OFF — licze na biblioteki systemowe")
    return()
endif()

set(_vcpkg_dir "${CMAKE_CURRENT_SOURCE_DIR}/.vcpkg")
set(_vcpkg_toolchain "${_vcpkg_dir}/scripts/buildsystems/vcpkg.cmake")

if(NOT EXISTS "${_vcpkg_toolchain}")
    find_program(GIT_EXECUTABLE git REQUIRED)
    if(NOT EXISTS "${_vcpkg_dir}/.git")
        message(STATUS "predeye: klonuje vcpkg do ${_vcpkg_dir} (jednorazowo)...")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 https://github.com/microsoft/vcpkg
                    "${_vcpkg_dir}"
            RESULT_VARIABLE _clone_res)
        if(NOT _clone_res EQUAL 0)
            message(FATAL_ERROR "predeye: nie udalo sie sklonowac vcpkg (kod ${_clone_res}). "
                                "Ustaw VCPKG_ROOT albo -DPREDEYE_AUTO_VCPKG=OFF.")
        endif()
    endif()

    if(WIN32)
        set(_bootstrap "${_vcpkg_dir}/bootstrap-vcpkg.bat")
    else()
        set(_bootstrap "${_vcpkg_dir}/bootstrap-vcpkg.sh")
    endif()
    message(STATUS "predeye: bootstrap vcpkg...")
    execute_process(COMMAND "${_bootstrap}" -disableMetrics
                    WORKING_DIRECTORY "${_vcpkg_dir}"
                    RESULT_VARIABLE _boot_res)
    if(NOT _boot_res EQUAL 0)
        message(FATAL_ERROR "predeye: bootstrap vcpkg nie powiodl sie (kod ${_boot_res}).")
    endif()
endif()

set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_toolchain}" CACHE STRING "Vcpkg toolchain (auto)")
message(STATUS "predeye: uzywam auto-vcpkg (${_vcpkg_toolchain})")
