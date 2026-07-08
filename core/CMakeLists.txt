find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(predeye_core STATIC
    models.cpp
    omeda_client.cpp
    hero_context.cpp
    build_engine.cpp
    enemy_build.cpp
)
target_include_directories(predeye_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(predeye_core PUBLIC nlohmann_json::nlohmann_json PRIVATE CURL::libcurl)

# Watki uzywane do pauzy miedzy zapytaniami (std::this_thread).
find_package(Threads REQUIRED)
target_link_libraries(predeye_core PRIVATE Threads::Threads)
