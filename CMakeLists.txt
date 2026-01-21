cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

project(GameEngine LANGUAGES C CXX)
# FetchContent for external dependencies
include(FetchContent)

# mimalloc: High-performance memory allocator
# Useful for profiling memory performance and reducing allocator overhead
# Optional: disable with -DENABLE_MIMALLOC=OFF
option(ENABLE_MIMALLOC "Enable mimalloc memory allocator" ON)

if(ENABLE_MIMALLOC)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG main  # Use main branch; pin to a tag like v2.1.2 for stability
        GIT_SHALLOW TRUE
    )
    
    set(MI_OVERRIDE ON CACHE BOOL "Override malloc/free with mimalloc" FORCE)
    set(MI_BUILD_SHARED ON CACHE BOOL "Build mimalloc as shared library" FORCE)
    set(MI_BUILD_STATIC OFF CACHE BOOL "Disable mimalloc static library" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Disable mimalloc tests" FORCE)
    
    FetchContent_MakeAvailable(mimalloc)
    message(STATUS "mimalloc enabled: Override malloc/free")
    set(MIMALLOC_INCLUDE_DIR "${mimalloc_SOURCE_DIR}/include")
else()
    message(STATUS "mimalloc disabled (use -DENABLE_MIMALLOC=ON to enable)")
    set(MIMALLOC_INCLUDE_DIR "")
endif()

# Common Settings
include_directories(${CMAKE_SOURCE_DIR}/include)

# Platform and Configuration Settings
add_library(LudusBuildSettings INTERFACE)

# Add mimalloc includes to build settings
if(MIMALLOC_INCLUDE_DIR)
    target_include_directories(LudusBuildSettings INTERFACE ${MIMALLOC_INCLUDE_DIR})
endif()

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

# Frame pointers: Always enabled in Debug for better profiling and stack traces
# Even when sanitizers are disabled, frame pointers help with:
# - Profiling tools (perf, VTune, etc.)
# - Crash dump analysis
# - Debug stack unwinding
target_compile_options(LudusBuildSettings INTERFACE
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-fno-omit-frame-pointer>
)

# Link mimalloc if enabled
if(ENABLE_MIMALLOC AND TARGET mimalloc)
    target_link_libraries(LudusBuildSettings INTERFACE mimalloc)
    message(STATUS "mimalloc linked to LudusBuildSettings")
endif()

# Sanitizers: Enabled by default in Debug and RelWithDebInfo modes for leak/memory detection
# LSan is implicitly enabled with ASan on Linux; on Windows (MSVC) we use /fsanitize=address
add_library(ProjectSanitizersSettings INTERFACE)

option(ENABLE_SANITIZERS "Enable {ASan,UBSan,LSan} sanitizers" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(ENABLE_SANITIZERS AND NOT MSVC)
        # GCC/Clang: ASan + UBSan + LSan (LSan is implicit with ASan on Linux, explicit on others)
        # -fno-omit-frame-pointer: Better stack traces for leak detection (also set unconditionally for Debug above)
        # -g: Debug symbols for better leak reports
        target_compile_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer -g>)
        target_link_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer>)
    elseif(ENABLE_SANITIZERS AND MSVC)
        # MSVC ASan (x64 only, no /GL - incompatible with optimizations)
        # Note: MSVC's ASan doesn't support LSan, but catches heap use-after-free, buffer overflows, etc.
        # MSVC /Oy- disables frame pointer omission (equivalent to -fno-omit-frame-pointer)
        add_compile_options($<$<AND:$<CXX_COMPILER_ID:MSVC>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:/fsanitize=address> /Zi /Oy- /EHsc)
        add_link_options($<$<AND:$<CXX_COMPILER_ID:MSVC>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:/INCREMENTAL> /DEBUG)
    endif()
endif()

target_link_libraries(LudusBuildSettings INTERFACE ProjectSanitizersSettings)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CONFIG_FOLDER "Debug")

    target_compile_definitions(LudusBuildSettings INTERFACE $<$<CONFIG:Debug>:LUDUS_DEBUG=1> )
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CONFIG_FOLDER "Release")

    target_compile_definitions(LudusBuildSettings INTERFACE $<$<CONFIG:Release>:LUDUS_RELEASE=1> )
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CONFIG_FOLDER "RelWithDebInfo")

    # RelWithDebInfo: Release with debug info (optimized + debuggable)
    # Used for profiling and leak testing with sanitizers
    target_compile_definitions(LudusBuildSettings INTERFACE $<$<CONFIG:RelWithDebInfo>:LUDUS_RELEASE=1> )
else()
    message(FATAL_ERROR "Unsupported build configuration: ${CMAKE_BUILD_TYPE}")
endif()

add_subdirectory(src)