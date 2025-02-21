cmake_minimum_required(VERSION 3.10)
project(QuillArt)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)

set(SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/renderer/Renderer.cpp
    src/tools/Tool.cpp
    src/ui/MainWindow.cpp
    src/vector/VectorObject.cpp
)

add_executable(QuillArt ${SOURCES})

target_link_libraries(QuillArt ${SDL2_LIBRARIES} ${OPENGL_gl_LIBRARY})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends)
