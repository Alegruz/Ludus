cmake_minimum_required(VERSION 3.15)

# ============================================================================
# Onboarding Check
# ============================================================================
# Verify that the user has run the onboarding/initialization script
# The onboarding script sets up git submodules, environment variables, and dependencies
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.ludus-initialized")
    message(STATUS "Launching Ludus onboarding GUI...")
    
    # Attempt to launch the onboarding GUI
    if(WIN32)
        # Windows: Run init.bat with --gui flag
        execute_process(
            COMMAND cmd.exe /c "${CMAKE_SOURCE_DIR}/init.bat" --gui
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE INIT_RESULT
        )
    else()
        # Unix/Linux/macOS: Run init.sh with --gui flag
        execute_process(
            COMMAND bash "${CMAKE_SOURCE_DIR}/init.sh" --gui
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE INIT_RESULT
        )
    endif()
    
    # Check if the marker file was created by the onboarding script
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.ludus-initialized")
        message(FATAL_ERROR
            "\n"
            "========================================================================\n"
            "CMake Configuration Error - Project Not Initialized\n"
            "========================================================================\n"
            "\n"
            "The onboarding script could not be automatically launched, or was\n"
            "cancelled. Please run the initialization script manually:\n"
            "\n"
            "  Windows:  init.bat\n"
            "  Linux/Mac: ./init.sh\n"
            "\n"
            "Or for the latest Vulkan SDK:\n"
            "  Windows:  init_with_latest_volk.bat\n"
            "\n"
            "The initialization script will:\n"
            "  - Set up git submodules\n"
            "  - Download required dependencies (Vulkan SDK, build tools, etc.)\n"
            "  - Configure your environment variables\n"
            "\n"
            "Run from the project root directory:\n"
            "  ${CMAKE_SOURCE_DIR}\n"
            "\n"
            "After running the init script, you can retry the CMake configuration.\n"
            "========================================================================\n"
        )
    endif()
endif()

# ============================================================================
# Common Error Helper
# ============================================================================
function(ludus_fatal_error)
    string(JOIN " " _LUDUS_FATAL_MSG ${ARGV})

    # Try to launch onboarding once to resolve missing prerequisites
    if(NOT DEFINED LUDUS_INIT_TRIED)
        set(LUDUS_INIT_TRIED TRUE PARENT_SCOPE)
        if(WIN32)
            execute_process(
                COMMAND cmd.exe /c "${CMAKE_SOURCE_DIR}/init.bat" --gui
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE _LUDUS_INIT_RESULT
            )
        else()
            execute_process(
                COMMAND bash "${CMAKE_SOURCE_DIR}/init.sh" --gui
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE _LUDUS_INIT_RESULT
            )
        endif()
    endif()

    message(FATAL_ERROR
        "\n"
        "========================================================================\n"
        "CMake Configuration Error\n"
        "========================================================================\n"
        "\n"
        "${_LUDUS_FATAL_MSG}\n"
        "\n"
        "An attempt was made to launch the onboarding script. If it did not\n"
        "run or was cancelled, please run it manually:\n"
        "  Windows:  init.bat\n"
        "  Linux/Mac: ./init.sh\n"
        "\n"
        "Then re-run CMake configuration.\n"
        "========================================================================\n"
    )
endfunction()

# Export compile_commands.json for clang-tidy integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================================
# Pre-Project Generator Fixup (Ninja)
# ============================================================================
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(_LUDUS_NINJA_CANDIDATE "")

    if(DEFINED CMAKE_MAKE_PROGRAM AND EXISTS "${CMAKE_MAKE_PROGRAM}")
        set(_LUDUS_NINJA_CANDIDATE "${CMAKE_MAKE_PROGRAM}")
    elseif(DEFINED CMAKE_MAKE_PROGRAM AND NOT EXISTS "${CMAKE_MAKE_PROGRAM}")
        set(_LUDUS_NINJA_CANDIDATE "")
    endif()

    if(NOT _LUDUS_NINJA_CANDIDATE)
        find_program(_LUDUS_NINJA_EXE NAMES ninja
            PATHS
                "${CMAKE_PROGRAM_PATH}"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
                "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
        )
        if(_LUDUS_NINJA_EXE)
            set(_LUDUS_NINJA_CANDIDATE "${_LUDUS_NINJA_EXE}")
        endif()
    endif()

    if(_LUDUS_NINJA_CANDIDATE)
        set(CMAKE_MAKE_PROGRAM "${_LUDUS_NINJA_CANDIDATE}" CACHE FILEPATH "Ninja executable" FORCE)
    else()
        # Try to launch onboarding before project() to avoid generator failure
        if(NOT DEFINED LUDUS_INIT_TRIED)
            set(LUDUS_INIT_TRIED TRUE CACHE INTERNAL "Init run attempted" FORCE)
            if(WIN32)
                execute_process(
                    COMMAND cmd.exe /c "${CMAKE_SOURCE_DIR}/init.bat" --gui
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    RESULT_VARIABLE _LUDUS_INIT_RESULT
                )
            else()
                execute_process(
                    COMMAND bash "${CMAKE_SOURCE_DIR}/init.sh" --gui
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    RESULT_VARIABLE _LUDUS_INIT_RESULT
                )
            endif()
        endif()
    endif()
endif()

project(GameEngine LANGUAGES C CXX)

# ============================================================================
# Generator Prerequisite Check (Ninja)
# ============================================================================
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(_LUDUS_NINJA_MISSING TRUE)

    if(DEFINED CMAKE_MAKE_PROGRAM AND EXISTS "${CMAKE_MAKE_PROGRAM}")
        set(_LUDUS_NINJA_MISSING FALSE)
    elseif(DEFINED CMAKE_MAKE_PROGRAM AND NOT EXISTS "${CMAKE_MAKE_PROGRAM}")
        set(_LUDUS_NINJA_MISSING TRUE)
    else()
        find_program(_LUDUS_NINJA_EXE NAMES ninja
            PATHS
                "${CMAKE_PROGRAM_PATH}"
                "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
                "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"
            NO_DEFAULT_PATH
        )
        if(_LUDUS_NINJA_EXE)
            set(_LUDUS_NINJA_MISSING FALSE)
        else()
            find_program(_LUDUS_NINJA_EXE_DEFAULT NAMES ninja)
            if(_LUDUS_NINJA_EXE_DEFAULT)
                set(_LUDUS_NINJA_MISSING FALSE)
            endif()
        endif()
    endif()

    if(_LUDUS_NINJA_MISSING)
        ludus_fatal_error("This preset uses the Ninja generator, but Ninja was not found.")
    endif()
endif()

# ============================================================================
# Build Configuration Options
# Define all options early so they can be referenced throughout the build
# ============================================================================
option(ENABLE_MIMALLOC "Enable mimalloc memory allocator" ON)
option(ENABLE_SANITIZERS "Enable ASan+UBSan+LSan (GCC/Clang only - MSVC sanitizers disabled)" ON)
option(ENABLE_APP_VERIFIER "Enable Windows Application Verifier setup (recommended for Windows)" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)
option(ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(ENABLE_MSVC_ANALYZE "Enable MSVC /analyze static analysis" OFF)

# Graphics API selection
set(GRAPHICS_API "Cpu" CACHE STRING "Graphics API to use (Cpu, Vulkan, D3D12, Metal)")
set_property(CACHE GRAPHICS_API PROPERTY STRINGS Cpu Vulkan D3D12 Metal)

# Validate graphics API selection
if(NOT GRAPHICS_API MATCHES "^(Cpu|Vulkan|D3D12|Metal)$")
    ludus_fatal_error("Invalid GRAPHICS_API: ${GRAPHICS_API}. Must be one of: Cpu, Vulkan, D3D12, Metal")
endif()

# Platform-specific validation
if(GRAPHICS_API STREQUAL "D3D12" AND NOT WIN32)
    ludus_fatal_error("D3D12 is only available on Windows")
endif()
if(GRAPHICS_API STREQUAL "Metal" AND NOT APPLE)
    ludus_fatal_error("Metal is only available on macOS")
endif()

message(STATUS "Graphics API: ${GRAPHICS_API}")

# Tool search hints (standard install locations):
set(LLVM_HINT_PATHS
    "C:/Program Files/LLVM/bin"
    "C:/Program Files (x86)/LLVM/bin"
    "$ENV{ProgramFiles}/LLVM/bin"
    "$ENV{ProgramFiles\(x86\)}/LLVM/bin"
    "$ENV{ProgramData}/chocolatey/bin"
    "$ENV{ChocolateyInstall}/bin"
)

# Helper: Find a dev tool (no auto-install; onboarding handles installs)
function(find_dev_tool VAR_NAME TOOL_NAME)
    find_program(${VAR_NAME} NAMES ${TOOL_NAME} HINTS ${LLVM_HINT_PATHS})
    
    if(NOT ${VAR_NAME})
        message(STATUS "${TOOL_NAME} not found. Run the onboarding app (init.bat/init.sh) to install prerequisites.")
    else()
        message(STATUS "${TOOL_NAME} found: ${${VAR_NAME}}")
    endif()
    
    # Export to parent scope
    set(${VAR_NAME} ${${VAR_NAME}} PARENT_SCOPE)
endfunction()

# FetchContent for external dependencies
include(FetchContent)

# ============================================================================
# Vulkan (volk) integration
# Primary Vulkan loader for Vulkan presets
# ============================================================================
if(GRAPHICS_API STREQUAL "Vulkan")
    # Vulkan-Headers (for vulkan/vulkan.h)
    # Allow override of VulkanHeaders and volk tags from command line

    if(NOT DEFINED VULKAN_HEADERS_TAG)
        set(VULKAN_HEADERS_TAG v1.3.275)
    endif()

    # Default volk tag (set via onboarding or -DVOLK_TAG=...)
    # Note: Volk uses vulkan-sdk version tags, not bare version numbers
    if(NOT DEFINED VOLK_TAG)
        set(VOLK_TAG vulkan-sdk-1.4.335.0)
    endif()

    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG ${VULKAN_HEADERS_TAG}
        GIT_SHALLOW TRUE
    )

    # volk: meta-loader for Vulkan
    set(VOLK_STATIC ON CACHE BOOL "Build volk as a static library" FORCE)
    set(VOLK_INSTALL OFF CACHE BOOL "Disable volk install step" FORCE)
    set(VOLK_TESTS OFF CACHE BOOL "Disable volk tests" FORCE)

    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG ${VOLK_TAG}
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(VulkanHeaders volk)

    add_library(LudusVulkanDeps INTERFACE)
    target_include_directories(LudusVulkanDeps INTERFACE
        ${VulkanHeaders_SOURCE_DIR}/include
        ${volk_SOURCE_DIR}
    )

    if(TARGET volk::volk)
        target_link_libraries(LudusVulkanDeps INTERFACE volk::volk)
    elseif(TARGET volk)
        target_link_libraries(LudusVulkanDeps INTERFACE volk)
    else()
        ludus_fatal_error("volk target not found after FetchContent_MakeAvailable")
    endif()
endif()

# ============================================================================
# mimalloc: High-performance memory allocator
# Useful for profiling memory performance and reducing allocator overhead
# Note: Automatically disabled when sanitizers are enabled to avoid conflicts
# ============================================================================

# Disable mimalloc when sanitizers are enabled to avoid malloc interception conflicts
if(ENABLE_SANITIZERS)
    set(ENABLE_MIMALLOC OFF CACHE BOOL "Mimalloc disabled when sanitizers are enabled" FORCE)
    message(STATUS "mimalloc automatically disabled because sanitizers are enabled (they conflict)")
endif()

if(ENABLE_MIMALLOC)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.2.7  # Pinned to stable release for reproducible builds
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
    ludus_fatal_error("Unsupported platform ${CMAKE_SYSTEM_NAME}")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" LUDUS_SYS_PROC_LOWER)
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" LUDUS_HOST_PROC_LOWER)

set(LUDUS_SYS_ARCH_GENERIC "unknown")
set(LUDUS_HOST_ARCH_GENERIC "unknown")

if(LUDUS_SYS_PROC_LOWER MATCHES "x86_64|amd64|x64")
    set(LUDUS_SYS_ARCH_GENERIC "x86_64")
elseif(LUDUS_SYS_PROC_LOWER MATCHES "i[3-6]86|x86")
    set(LUDUS_SYS_ARCH_GENERIC "x86")
elseif(LUDUS_SYS_PROC_LOWER MATCHES "aarch64|arm64")
    set(LUDUS_SYS_ARCH_GENERIC "arm64")
elseif(LUDUS_SYS_PROC_LOWER MATCHES "armv[7-9]|arm")
    set(LUDUS_SYS_ARCH_GENERIC "arm")
elseif(LUDUS_SYS_PROC_LOWER MATCHES "ppc64")
    set(LUDUS_SYS_ARCH_GENERIC "ppc64")
elseif(LUDUS_SYS_PROC_LOWER MATCHES "ppc")
    set(LUDUS_SYS_ARCH_GENERIC "ppc")
endif()

if(LUDUS_HOST_PROC_LOWER MATCHES "x86_64|amd64|x64")
    set(LUDUS_HOST_ARCH_GENERIC "x86_64")
elseif(LUDUS_HOST_PROC_LOWER MATCHES "i[3-6]86|x86")
    set(LUDUS_HOST_ARCH_GENERIC "x86")
elseif(LUDUS_HOST_PROC_LOWER MATCHES "aarch64|arm64")
    set(LUDUS_HOST_ARCH_GENERIC "arm64")
elseif(LUDUS_HOST_PROC_LOWER MATCHES "armv[7-9]|arm")
    set(LUDUS_HOST_ARCH_GENERIC "arm")
elseif(LUDUS_HOST_PROC_LOWER MATCHES "ppc64")
    set(LUDUS_HOST_ARCH_GENERIC "ppc64")
elseif(LUDUS_HOST_PROC_LOWER MATCHES "ppc")
    set(LUDUS_HOST_ARCH_GENERIC "ppc")
endif()

target_compile_definitions(LudusBuildSettings INTERFACE
  # Platform
  $<$<PLATFORM_ID:Windows>:LUDUS_WINDOWS=1>
  $<$<PLATFORM_ID:Linux>:LUDUS_LINUX=1>
  $<$<PLATFORM_ID:Darwin>:LUDUS_MAC=1>

  # Config
  $<$<CONFIG:Debug>:LUDUS_DEBUG=1>
  $<$<CONFIG:Release>:LUDUS_RELEASE=1>

  # Graphics API
  $<$<STREQUAL:${GRAPHICS_API},Cpu>:LUDUS_GRAPHICS_CPU=1>
  $<$<STREQUAL:${GRAPHICS_API},Vulkan>:LUDUS_GRAPHICS_VULKAN=1>
  $<$<STREQUAL:${GRAPHICS_API},D3D12>:LUDUS_GRAPHICS_D3D12=1>
  $<$<STREQUAL:${GRAPHICS_API},Metal>:LUDUS_GRAPHICS_METAL=1>

  # Feature toggles (examples)
  LUDUS_ENABLE_ASSERTS=1

  # CMake configure-time hardware/system info
  LUDUS_CMAKE_SYSTEM_NAME=\"${CMAKE_SYSTEM_NAME}\"
  LUDUS_CMAKE_SYSTEM_ARCH=\"${LUDUS_SYS_ARCH_GENERIC}\"
  LUDUS_CMAKE_HOST_SYSTEM_NAME=\"${CMAKE_HOST_SYSTEM_NAME}\"
  LUDUS_CMAKE_HOST_SYSTEM_ARCH=\"${LUDUS_HOST_ARCH_GENERIC}\"
  LUDUS_CMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P}
)

message(STATUS "Ludus CMake hardware macros:")
message(STATUS "  LUDUS_CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
message(STATUS "  LUDUS_CMAKE_SYSTEM_ARCH=${LUDUS_SYS_ARCH_GENERIC}")
message(STATUS "  LUDUS_CMAKE_HOST_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}")
message(STATUS "  LUDUS_CMAKE_HOST_SYSTEM_ARCH=${LUDUS_HOST_ARCH_GENERIC}")
message(STATUS "  LUDUS_CMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P}")

target_compile_options(LudusBuildSettings INTERFACE
    # MSVC
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /Zc:__cplusplus /EHsc /utf-8 /Zc:preprocessor /bigobj>
    # GCC/Clang
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -fno-exceptions -fno-rtti>  # if that?�s your policy; otherwise drop
)

# ============================================================================
# Static Analysis Tools
# Dev tool detection (clang-tidy, clang-format)
# Options defined at top of file
# ============================================================================

# Find or install clang-format
find_dev_tool(CLANG_FORMAT_EXE clang-format)
set(ENABLE_CLANG_FORMAT ${CLANG_FORMAT_EXE})

# Find or install clang-tidy (optional, for static analysis)
if(ENABLE_CLANG_TIDY)
    find_dev_tool(CLANG_TIDY_EXE clang-tidy)
    if(CLANG_TIDY_EXE)
        # Suppress warnings from external dependencies (mimalloc, etc.) to avoid false positives
        # --header-filter only analyzes project headers (not external deps like mimalloc)
        # --system-headers=false prevents checking system/external headers
        # --checks=-clang-diagnostic-* must come before -warnings-as-errors to properly exclude compiler diagnostics
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--checks=-clang-diagnostic-*;-warnings-as-errors=*;--header-filter=.*include/Ludus/.*;--system-headers=false")
        message(STATUS "clang-tidy enabled (warnings as errors): ${CLANG_TIDY_EXE}")
    else()
        message(STATUS "clang-tidy disabled (not found). Run onboarding to install prerequisites.")
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
    message(STATUS "clang-format not found - formatting checks disabled. Run onboarding to install prerequisites.")
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
            "--suppress=useStlAlgorithm"
            "--suppress=missingIncludeSystem"
            "--suppress=*:${CMAKE_BINARY_DIR}/_deps/*"
            "--suppress=*:${CMAKE_BINARY_DIR}/out/_deps/*"
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

# ============================================================================
# Compiler and Platform Information
# ============================================================================
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

# ============================================================================
# Sanitizers Configuration
# Only enabled for GCC/Clang (MSVC AddressSanitizer disabled due to compatibility issues)
# Options defined at top of file
# ============================================================================
add_library(ProjectSanitizersSettings INTERFACE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(ENABLE_SANITIZERS AND NOT MSVC)
        # GCC/Clang: ASan + UBSan + LSan
        target_compile_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,$<BOOL:${ENABLE_SANITIZERS}>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer -g>)
        target_link_options(ProjectSanitizersSettings INTERFACE 
            $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,$<BOOL:${ENABLE_SANITIZERS}>>:-fsanitize=address,undefined,leak -fno-omit-frame-pointer>)
        # Disable PIE on Linux with ASan to avoid shadow memory conflicts (Linux 4.12+ issue)
        if(UNIX AND NOT APPLE)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -no-pie")
        endif()
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
    ludus_fatal_error("Unsupported build configuration: ${CMAKE_BUILD_TYPE}")
endif()

add_subdirectory(src)



