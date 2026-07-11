# Modul przenosny; Windows-only jest tylko capture_dxgi (guard WIN32).
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(predeye_vision STATIC
    icon_matcher.cpp
    calibration.cpp
    capture.cpp
    scoreboard_reader.cpp
)
if(WIN32)
    target_sources(predeye_vision PRIVATE capture_dxgi.cpp)
    target_link_libraries(predeye_vision PRIVATE d3d11 dxgi)
endif()
target_include_directories(predeye_vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(predeye_vision
    PUBLIC predeye_core opencv_core
    PRIVATE opencv_imgproc opencv_imgcodecs)
