cmake_minimum_required(VERSION 3.20)
project(TextureExplorerMac LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Oldest macOS version supported")

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
FetchContent_Declare(metalcpp
    URL      https://developer.apple.com/metal/cpp/files/metal-cpp_26.zip
    URL_HASH SHA256=4df3c078b9aadcb516212e9cb03004cbc5ce9a3e9c068fa3144d021db585a3a4)
FetchContent_MakeAvailable(imgui stb json metalcpp)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm
    ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${metalcpp_SOURCE_DIR})
# The Metal back end then accepts metal-cpp objects (MTL::Device*, ...) from C++ code.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_METAL_CPP)

add_executable(TextureExplorer MACOSX_BUNDLE
    src/main.mm
    src/App.cpp
    src/Renderer.cpp
    src/Material.cpp
    src/Mesh.cpp
    src/Inspector.cpp)
target_include_directories(TextureExplorer PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(TextureExplorer PRIVATE imgui nlohmann_json::nlohmann_json
    "-framework Cocoa" "-framework Metal" "-framework MetalKit" "-framework QuartzCore"
    "-framework GameController" "-framework CoreServices")
get_filename_component(TEXEX_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
target_compile_definitions(TextureExplorer PRIVATE TEXEX_ROOT="${TEXEX_ROOT}")
target_compile_options(TextureExplorer PRIVATE -Wall -Wextra)
set_target_properties(TextureExplorer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Texture Explorer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "edu.uniandes.TextureExplorer")
