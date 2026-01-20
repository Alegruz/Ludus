cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

project(GameEngine LANGUAGES C CXX)

# Common Settings
include_directories(${CMAKE_SOURCE_DIR}/include)

# Platform and Configuration Settings
add_library(LudusBuildSettings INTERFACE)

if(WIN32)
    set(PLATFORM_FOLDER "Windows")
    set(CMAKE_WIN32_EXECUTABLE true)
    
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
elseif(APPLE)
    set(PLATFORM_FOLDER "MacOs")
elseif(UNIX)
    set(PLATFORM_FOLDER "Unix")
else()
    message(FATAL_ERROR "Unsupported platform ${CMAKE_SYSTEM_NAME}")
endif()

target_compile_definitions(LudusBuildSettings INTERFACE
  # Platform
  $<$<PLATFORM_ID:Windows>:LUDUS_WINDOWS=1>
  $<$<PLATFORM_ID:Linux>:LUDUS_LINUX=1>
  $<$<PLATFORM_ID:Darwin>:LUDUS_MAC=1>

  # Config
  $<$<CONFIG:Debug>:LUDUS_DEBUG=1>
  $<$<CONFIG:Release>:LUDUS_RELEASE=1>

  # Feature toggles (examples)
  LUDUS_ENABLE_ASSERTS=1
)

target_compile_options(LudusBuildSettings INTERFACE
    # MSVC
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /Zc:__cplusplus /EHsc /utf-8 /Zc:preprocessor /bigobj>
    # GCC/Clang
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -fno-exceptions -fno-rtti>  # if that’s your policy; otherwise drop
)

message(STATUS "Current platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Current compiler: ${CMAKE_CXX_COMPILER_ID}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_library(ProjectSanitizersSettings INTERFACE)

    option(ENABLE_SANITIZERS "Enable {ASan,UBSan,TSan}" OFF)

    if(ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(ProjectSanitizersSettings INTERFACE $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-fsanitize=address,undefined -fno-omit-frame-pointer -g>)
        target_link_options(ProjectSanitizersSettings INTERFACE $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    endif()
endif()

# MSVC ASan (x64 only, no /GL)
if(ENABLE_SANITIZERS AND MSVC)
    add_compile_options(/fsanitize=address /Zi /EHsc)
    add_link_options(/INCREMENTAL /DEBUG)
endif()

if(ENABLE_SANITIZERS)
    target_link_libraries(LudusBuildSettings PRIVATE ProjectSanitizersSettings)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CONFIG_FOLDER "Debug")

    target_compile_definitions(LudusBuildSettings INTERFACE $<$<CONFIG:Debug>:LUDUS_DEBUG=1> )
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CONFIG_FOLDER "Release")

    target_compile_definitions(LudusBuildSettings INTERFACE $<$<CONFIG:Release>:LUDUS_RELEASE=1> )
else()
    message(FATAL_ERROR "Unsupported build configuration: ${CMAKE_BUILD_TYPE}")
endif()

add_subdirectory(src)