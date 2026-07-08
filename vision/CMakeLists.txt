# icon_matcher jest przenosny; Windows-only bedzie tylko capture_dxgi (M3).
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(predeye_vision STATIC
    icon_matcher.cpp
)
target_include_directories(predeye_vision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(predeye_vision
    PUBLIC predeye_core opencv_core
    PRIVATE opencv_imgproc opencv_imgcodecs)
