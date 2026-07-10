find_package(doctest REQUIRED)

# Testy rdzenia zawsze; testy toru wizyjnego tylko przy PREDEYE_VISION.
set(_test_sources
    test_main.cpp
    test_models.cpp
    test_hero_context.cpp
    test_build_engine.cpp
    test_enemy_build.cpp
)
if(PREDEYE_VISION)
    list(APPEND _test_sources test_icon_matcher.cpp test_calibration.cpp)
endif()

add_executable(predeye_tests ${_test_sources})
target_link_libraries(predeye_tests PRIVATE predeye_core doctest::doctest)
if(PREDEYE_VISION)
    target_link_libraries(predeye_tests PRIVATE predeye_vision opencv_imgproc opencv_imgcodecs)
endif()
target_compile_definitions(predeye_tests
    PRIVATE PREDEYE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME predeye_tests COMMAND predeye_tests)
