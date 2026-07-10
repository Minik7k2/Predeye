add_executable(predeye main.cpp)
target_link_libraries(predeye PRIVATE predeye_core predeye_vision opencv_imgcodecs)
