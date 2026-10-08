cmake_minimum_required(VERSION 3.20)
project(TextureExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b-docking
    GIT_SHALLOW    TRUE)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(imgui stb json)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

add_executable(TextureExplorer WIN32
    src/main.cpp
    src/App.cpp
    src/Renderer.cpp
    src/Material.cpp
    src/Mesh.cpp
    src/Inspector.cpp)
target_include_directories(TextureExplorer PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(TextureExplorer PRIVATE imgui nlohmann_json::nlohmann_json
    d3d11 dxgi d3dcompiler shell32)
target_compile_definitions(TextureExplorer PRIVATE
    TEXEX_ROOT="${CMAKE_SOURCE_DIR}" UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(TextureExplorer PRIVATE /W4 /permissive- /utf-8)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TextureExplorer)
endif()
