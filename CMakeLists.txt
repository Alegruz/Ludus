cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
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
    
    target_compile_definitions(LudusBuildSettings INTERFACE $<$<PLATFORM_ID:Windows>:LUDUS_WINDOWS=1> )
elseif(APPLE)
    set(PLATFORM_FOLDER "MacOs")

    target_compile_definitions(LudusBuildSettings INTERFACE $<$<PLATFORM_ID:Darwin>:LUDUS_MAC=1> )
elseif(UNIX)
    set(PLATFORM_FOLDER "Unix")
    
    target_compile_definitions(LudusBuildSettings INTERFACE $<$<PLATFORM_ID:Linux>:LUDUS_LINUX=1> )
else()
    message(FATAL_ERROR "Unsupported platform ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Current platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Current compiler: ${CMAKE_CXX_COMPILER_ID}")

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