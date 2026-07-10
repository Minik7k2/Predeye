# Dear ImGui (vendored) + backendy GLFW/OpenGL3. Zbudowane jako statyczna
# biblioteka, by GUI mialo minimum zaleznosci zewnetrznych (tylko GLFW + GL).
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# Kod dostawcy — nie egzekwujemy na nim naszego /W4.
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()
