cmake_minimum_required(VERSION 3.15)

# Export compile_commands.json for clang-tidy integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

project(GameEngine LANGUAGES C CXX)

# Tool search hints (standard install locations):
set(LLVM_HINT_PATHS
    "C:/Program Files/LLVM/bin"
    "C:/Program Files (x86)/LLVM/bin"
    "$ENV{ProgramFiles}/LLVM/bin"
    "$ENV{ProgramFiles\(x86\)}/LLVM/bin"
    "$ENV{ProgramData}/chocolatey/bin"
    "$ENV{ChocolateyInstall}/bin"
)

# Helper: Find a dev tool and attempt auto-install if missing
# Auto-install enhances user convenience for clang-format/clang-tidy
function(find_or_install_dev_tool VAR_NAME TOOL_NAME)
    find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
    
    if(NOT ${VAR_NAME})
        message(STATUS "${TOOL_NAME} not found. Attempting auto-install...")
        
        if(WIN32)
            # Try Chocolatey first
            execute_process(
                COMMAND choco install llvm -y --no-progress
                RESULT_VARIABLE CHOCO_RESULT
                OUTPUT_QUIET ERROR_QUIET
            )
            if(CHOCO_RESULT EQUAL 0)
                find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
                if(${VAR_NAME})
                    message(STATUS "  ✓ ${TOOL_NAME} installed via Chocolatey: ${${VAR_NAME}}")
                else()
                    message(STATUS "  ✗ Install via Chocolatey succeeded but tool not found in PATH. Run: choco install llvm -y")
                endif()
            else()
                # Fall back to PowerShell installer
                set(LLVM_INSTALLER "${CMAKE_SOURCE_DIR}/tools/install-llvm.ps1")
                if(EXISTS "${LLVM_INSTALLER}")
                    message(STATUS "  Chocolatey unavailable. Trying: powershell -ExecutionPolicy Bypass -File tools/install-llvm.ps1")
                    execute_process(
                        COMMAND powershell -ExecutionPolicy Bypass -File "${LLVM_INSTALLER}"
                        RESULT_VARIABLE PS_RESULT
                        OUTPUT_QUIET ERROR_QUIET
                    )
                    if(PS_RESULT EQUAL 0)
                        find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
                        if(${VAR_NAME})
                            message(STATUS "  ✓ ${TOOL_NAME} installed via PowerShell script: ${${VAR_NAME}}")
                        endif()
                    else()
                        message(STATUS "  ✗ PowerShell install failed. Requires admin. Manual install: choco install llvm -y")
                    endif()
                endif()
            endif()
        elseif(APPLE)
            execute_process(
                COMMAND brew install llvm
                RESULT_VARIABLE BREW_RESULT
                OUTPUT_QUIET ERROR_QUIET
            )
            if(BREW_RESULT EQUAL 0)
                find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
                if(${VAR_NAME})
                    message(STATUS "  ✓ ${TOOL_NAME} installed via Homebrew: ${${VAR_NAME}}")
                else()
                    message(STATUS "  ✗ Homebrew install succeeded but tool not found. Run: brew install llvm")
                endif()
            else()
                message(STATUS "  ✗ Homebrew install failed. Manual: brew install llvm")
            endif()
        elseif(UNIX AND NOT APPLE)
            execute_process(
                COMMAND sudo apt-get update
                OUTPUT_QUIET ERROR_QUIET
            )
            execute_process(
                COMMAND sudo apt-get install -y clang-tools
                RESULT_VARIABLE APT_RESULT
                OUTPUT_QUIET ERROR_QUIET
            )
            if(APT_RESULT EQUAL 0)
                find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
                if(${VAR_NAME})
                    message(STATUS "  ✓ ${TOOL_NAME} installed via apt: ${${VAR_NAME}}")
                else()
                    message(STATUS "  ✗ Apt install succeeded but tool not found. Run: sudo apt-get install ${TOOL_NAME}")
                endif()
            else()
                message(STATUS "  ✗ Apt install failed. Manual: sudo apt-get install clang-tools")
            endif()
        endif()
    else()
        message(STATUS "${TOOL_NAME} found: ${${VAR_NAME}}")
    endif()
    
    # Export to parent scope
    set(${VAR_NAME} ${${VAR_NAME}} PARENT_SCOPE)
endfunction()

# FetchContent for external dependencies
include(FetchContent)

# mimalloc: High-performance memory allocator
# Useful for profiling memory performance and reducing allocator overhead
# Optional: disable with -DENABLE_MIMALLOC=OFF
# Note: Automatically disabled when sanitizers are enabled to avoid conflicts
option(ENABLE_MIMALLOC "Enable mimalloc memory allocator" ON)

# Disable mimalloc when sanitizers are enabled to avoid malloc interception conflicts
if(ENABLE_SANITIZERS)
    set(ENABLE_MIMALLOC OFF CACHE BOOL "Mimalloc disabled when sanitizers are enabled" FORCE)
    message(STATUS "mimalloc automatically disabled because sanitizers are enabled (they conflict)")
endif()

if(ENABLE_MIMALLOC)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.1.2  # Pinned to stable release for reproducible builds
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
# Static Analysis: clang-tidy, cppcheck, MSVC /analyze, clang-format
# Optional dev tools with auto-install for convenience
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)
option(ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(ENABLE_MSVC_ANALYZE "Enable MSVC /analyze static analysis" OFF)

# Find or install clang-format
find_or_install_dev_tool(CLANG_FORMAT_EXE clang-format)
set(ENABLE_CLANG_FORMAT ${CLANG_FORMAT_EXE})

# Find or install clang-tidy (optional, for static analysis)
if(ENABLE_CLANG_TIDY)
    find_or_install_dev_tool(CLANG_TIDY_EXE clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*")
        message(STATUS "clang-tidy enabled (warnings as errors): ${CLANG_TIDY_EXE}")
    else()
        message(STATUS "clang-tidy disabled (not found)")
        set(ENABLE_CLANG_TIDY OFF)
    endif()
endif()

# clang-format targets (if available)
if(ENABLE_CLANG_FORMAT)
    message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
    
    # Add a custom target to check formatting
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.c
        ${CMAKE_SOURCE_DIR}/include/*.h
        ${CMAKE_SOURCE_DIR}/include/*.hpp
    )
    
    add_custom_target(
        format-check
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/CheckFormat.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking code formatting with clang-format (fails on errors)"
    )
    
    add_custom_target(
        format-fix
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Fixing code formatting with clang-format"
    )
    
    message(STATUS "clang-format targets added: format-check, format-fix")
else()
    message(STATUS "clang-format not found - formatting checks disabled")
endif()

# cppcheck (optional static analysis)
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK 
            "${CPPCHECK_EXE}"
            "--enable=warning,performance,portability,style"
            "--inconclusive"
            "--inline-suppr"
            "--suppress=missingIncludeSystem"
            "--error-exitcode=0"
        )
        message(STATUS "cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "cppcheck requested but not found")
    endif()
endif()

if(ENABLE_MSVC_ANALYZE AND MSVC)
    target_compile_options(LudusBuildSettings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/analyze>
    )
    message(STATUS "MSVC /analyze enabled")
endif()
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

# Sanitizers: Only enabled for GCC/Clang (MSVC AddressSanitizer disabled due to compatibility issues)
add_library(ProjectSanitizersSettings INTERFACE)

option(ENABLE_SANITIZERS "Enable ASan+UBSan+LSan (GCC/Clang only - MSVC sanitizers disabled)" ON)
option(ENABLE_APP_VERIFIER "Enable Windows Application Verifier setup (recommended for Windows)" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(ENABLE_SANITIZERS AND NOT MSVC)
        # GCC/Clang: ASan + UBSan + LSan
        target_compile_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,$<BOOL:${ENABLE_SANITIZERS}>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer -g>)
        target_link_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,$<BOOL:${ENABLE_SANITIZERS}>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer>)
        message(STATUS "AddressSanitizer enabled for GCC/Clang")
    elseif(ENABLE_SANITIZERS AND MSVC)
        # MSVC AddressSanitizer: DISABLED due to Windows 11 compatibility issues
        message(STATUS "MSVC AddressSanitizer disabled (use -DENABLE_APP_VERIFIER=ON for memory debugging)")
    endif()
    
    # Windows Application Verifier - Recommended alternative to MSVC ASan
    if(WIN32 AND ENABLE_APP_VERIFIER)
        message(STATUS "Application Verifier enabled. Configure via: appverif /verify LudusEditor.exe")
        message(STATUS "Recommended AppVerif tests: Heaps Handles Locks TLS DangerousAPIs Networking")
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