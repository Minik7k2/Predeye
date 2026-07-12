# Graficzna powloka predeye — osobny plik wykonywalny (obok CLI predeye).
add_executable(predeye-gui main_gui.cpp)
target_include_directories(predeye-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(predeye-gui PRIVATE predeye_core imgui)

find_package(Threads REQUIRED)
target_link_libraries(predeye-gui PRIVATE Threads::Threads)

# Tor wizyjny opcjonalny: zakladki Kalibracja/Live wymagaja OpenCV (§4).
# Bez PREDEYE_VISION GUI ma tylko Build/Counter (rdzen przenosny).
if(PREDEYE_VISION)
    target_link_libraries(predeye-gui PRIVATE predeye_vision opencv_imgproc opencv_imgcodecs)
    target_compile_definitions(predeye-gui PRIVATE PREDEYE_HAS_VISION)
endif()

# Na Windows GUI to aplikacja okienkowa (bez konsoli w tle) — ale zostawiamy
# konsole widoczna, bo diagnostyka (stderr) jest przydatna. Zmien na WIN32,
# jesli chcesz ukryc okno konsoli.
