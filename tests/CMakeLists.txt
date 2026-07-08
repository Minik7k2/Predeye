find_package(doctest REQUIRED)

add_executable(predeye_tests
    test_main.cpp
    test_models.cpp
    test_hero_context.cpp
    test_build_engine.cpp
    test_enemy_build.cpp
)
target_link_libraries(predeye_tests PRIVATE predeye_core doctest::doctest)
target_compile_definitions(predeye_tests
    PRIVATE PREDEYE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME predeye_tests COMMAND predeye_tests)
