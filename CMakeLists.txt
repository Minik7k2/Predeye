cmake_minimum_required(VERSION 3.21)
project(predeye VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Wysoki poziom ostrzeżeń — ostrzeżenie = do naprawy przed commitem (§3.4).
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(app)
