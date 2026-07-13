# Graficzna powloka predeye — osobny plik wykonywalny (obok CLI predeye).
# Wymaga toru wizyjnego (zakladki Kalibracja i Live) — gating na PREDEYE_VISION
# jest w CMakeLists korzenia. Cele opencv_* pochodza z find_package tamze.
add_executable(predeye-gui main_gui.cpp)
target_include_directories(predeye-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(predeye-gui PRIVATE predeye_core predeye_vision imgui opencv_imgproc)

find_package(Threads REQUIRED)
target_link_libraries(predeye-gui PRIVATE Threads::Threads)

if(WIN32)
    # Natywny dialog "Otworz plik" (GetOpenFileNameW) w zakladce Kalibracja.
    target_link_libraries(predeye-gui PRIVATE comdlg32)
endif()

# Na Windows GUI to aplikacja okienkowa (bez konsoli w tle) — ale zostawiamy
# konsole widoczna, bo diagnostyka (stderr) jest przydatna. Zmien na WIN32,
# jesli chcesz ukryc okno konsoli.
