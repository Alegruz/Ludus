cmake_minimum_required(VERSION 3.29)
project(LudusWebGpuProbe LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Use the pinned Emscripten toolchain; this is a standalone browser probe")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(LUDUS_WARNINGS_AS_ERRORS ON)
include(../../cmake/EngineCompilerWarnings.cmake)
ludus_configure_project_warnings(ludus_probe_warnings)

add_executable(ludus_webgpu_probe main.cpp)
target_include_directories(ludus_webgpu_probe PRIVATE ../../modules/foundation/base/include)
target_link_libraries(ludus_webgpu_probe PRIVATE ludus_probe_warnings)
target_compile_options(ludus_webgpu_probe PRIVATE -fno-exceptions --use-port=emdawnwebgpu:cpp_bindings=false)
target_link_options(ludus_webgpu_probe PRIVATE
    -fno-exceptions --use-port=emdawnwebgpu:cpp_bindings=false
    -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=web
    -sASSERTIONS=1
)
set_target_properties(ludus_webgpu_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
configure_file(shell.html index.html COPYONLY)
