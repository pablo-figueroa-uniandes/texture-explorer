# Builds probe.cpp twice, against the Windows sources and against the macOS port, and runs
# compare.py on the two results:
#     cmake -S mac/compare -B mac/compare/build && cmake --build mac/compare/build --target compare
cmake_minimum_required(VERSION 3.20)
project(TextureExplorerCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(TEXEX_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)

include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(metalcpp
    URL      https://developer.apple.com/metal/cpp/files/metal-cpp_26.zip
    URL_HASH SHA256=4df3c078b9aadcb516212e9cb03004cbc5ce9a3e9c068fa3144d021db585a3a4)
FetchContent_Declare(directxmath
    GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
    GIT_TAG        jun2026
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  none)  # headers only: do not add its own CMake project
FetchContent_MakeAvailable(stb json metalcpp directxmath)

# The Windows version's own files, unchanged. DirectXMath is used without SSE/NEON
# intrinsics, i.e. its portable scalar code.
add_executable(probe_windows probe.cpp
    ${TEXEX_ROOT}/src/Mesh.cpp ${TEXEX_ROOT}/src/Material.cpp ${TEXEX_ROOT}/src/Inspector.cpp)
target_include_directories(probe_windows PRIVATE shims ${directxmath_SOURCE_DIR}/Inc ${TEXEX_ROOT}/src
    ${stb_SOURCE_DIR})
target_compile_definitions(probe_windows PRIVATE TEXEX_CORE_WINDOWS _XM_NO_INTRINSICS_)
target_link_libraries(probe_windows PRIVATE nlohmann_json::nlohmann_json)

add_executable(probe_mac probe.cpp
    ${TEXEX_ROOT}/mac/src/Mesh.cpp ${TEXEX_ROOT}/mac/src/Material.cpp ${TEXEX_ROOT}/mac/src/Inspector.cpp)
target_include_directories(probe_mac PRIVATE ${TEXEX_ROOT}/mac/src ${metalcpp_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(probe_mac PRIVATE nlohmann_json::nlohmann_json
    "-framework Foundation" "-framework Metal" "-framework QuartzCore")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(compare
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/compare.py
        $<TARGET_FILE:probe_windows> $<TARGET_FILE:probe_mac> ${TEXEX_ROOT}
    DEPENDS probe_windows probe_mac
    USES_TERMINAL)
