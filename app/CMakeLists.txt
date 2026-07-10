add_executable(predeye main.cpp)
target_link_libraries(predeye PRIVATE predeye_core)

# Tor wizyjny opcjonalny: bez niego CLI ma tylko build/counter (§4).
if(PREDEYE_VISION)
    target_link_libraries(predeye PRIVATE predeye_vision opencv_imgcodecs)
    target_compile_definitions(predeye PRIVATE PREDEYE_HAS_VISION)
endif()
