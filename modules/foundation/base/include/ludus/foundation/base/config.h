#pragma once

// -----------------------------------------------------------------------------
// Ludus Band 0 configuration macros: operating system, CPU architecture, and
// build flavor. Macros only — this header contains NO code, allocates nothing,
// and instantiates nothing, so it is effectively free to parse.
//
// This is the single foundational home for "what am I building for?" so that
// portability decisions (macOS, Vulkan/Metal/D3D12, SIMD, atomics width) can be
// made at the foundation without depending upward on the platform/windowing
// module. It is pulled in by <ludus/foundation/base/core.h>; include core.h
// (or this header directly) rather than relying on it arriving transitively.
//
// See docs/architecture/foundational-headers.md (§7, §8) and ADR 0007.
//
// NOTE: OS *family* detection lives here. Selecting a concrete windowing
// backend (Wayland/X11/headless/...) is the platform module's job and stays in
// <ludus/platform/config.h>; do not add backend selection here.
// -----------------------------------------------------------------------------

// Detection outputs are owned by this header. Build flavor/backend inputs remain
// build-system owned. Inactive legacy flags stay undefined for #ifdef consumers.
#if defined(LUDUS_TARGET_OS) || defined(LUDUS_TARGET_ARCH) || defined(LUDUS_POINTER_BITS) ||                           \
    defined(LUDUS_TARGET_ENDIAN) || defined(LUDUS_PLATFORM_WEB) || defined(LUDUS_PLATFORM_WINDOWS) ||                  \
    defined(LUDUS_PLATFORM_MACOS) || defined(LUDUS_PLATFORM_LINUX) || defined(LUDUS_PLATFORM_ANDROID) ||               \
    defined(LUDUS_PLATFORM_IOS) || defined(LUDUS_PLATFORM_DESKTOP) || defined(LUDUS_ARCH_WASM32) ||                    \
    defined(LUDUS_ARCH_WASM64) || defined(LUDUS_ARCH_X86_64) || defined(LUDUS_ARCH_ARM64)
#    error "Ludus target detection outputs must not be overridden"
#endif

// Numeric selectors for new #if decisions; use -Wundef to catch misspellings.
#define LUDUS_OS_WEB 1
#define LUDUS_OS_WINDOWS 2
#define LUDUS_OS_MACOS 3
#define LUDUS_OS_LINUX 4
#define LUDUS_OS_ANDROID 5
#define LUDUS_OS_IOS 6

// More-specific environments precede their compatibility families. Emscripten
// denotes the toolchain environment, not the user's browser or host OS.
#if defined(__EMSCRIPTEN__)
#    define LUDUS_TARGET_OS LUDUS_OS_WEB
#    define LUDUS_PLATFORM_WEB 1
#elif defined(__ANDROID__)
#    define LUDUS_TARGET_OS LUDUS_OS_ANDROID
#    define LUDUS_PLATFORM_ANDROID 1
#elif defined(_WIN32)
#    define LUDUS_TARGET_OS LUDUS_OS_WINDOWS
#    define LUDUS_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
// This target-SDK header contains macros only; TARGET_OS_MAC includes iOS and
// must not be used to select macOS. Catalyst is classified as an iOS API target.
#    include <TargetConditionals.h>
#    if TARGET_OS_OSX
#        define LUDUS_TARGET_OS LUDUS_OS_MACOS
#        define LUDUS_PLATFORM_MACOS 1
#    elif TARGET_OS_IOS
#        define LUDUS_TARGET_OS LUDUS_OS_IOS
#        define LUDUS_PLATFORM_IOS 1
#    else
#        error "Ludus target detection: unsupported Apple operating system"
#    endif
#elif defined(__linux__)
#    define LUDUS_TARGET_OS LUDUS_OS_LINUX
#    define LUDUS_PLATFORM_LINUX 1
#else
#    error "Ludus target detection: unsupported operating system"
#endif

// CMake supplies its target OS (never CMAKE_HOST_SYSTEM_NAME) to engine/SDK
// consumers. Standalone users need no build-generated configuration header.
#if defined(LUDUS_EXPECTED_TARGET_OS)
#    if LUDUS_TARGET_OS != LUDUS_EXPECTED_TARGET_OS
#        error "Ludus target detection: compiler target disagrees with the configured SDK target"
#    endif
#endif

#if defined(LUDUS_PLATFORM_WINDOWS) || defined(LUDUS_PLATFORM_MACOS) || defined(LUDUS_PLATFORM_LINUX)
#    define LUDUS_PLATFORM_DESKTOP 1
#endif

#define LUDUS_CPU_WASM32 1
#define LUDUS_CPU_WASM64 2
#define LUDUS_CPU_X86_64 3
#define LUDUS_CPU_ARM64 4

// ARM64EC also advertises x64 compatibility macros; it needs its own reviewed
// ABI/intrinsics policy and must never silently select the x86 implementation.
#if defined(_M_ARM64EC) || defined(__arm64ec__)
#    error "Ludus target detection: ARM64EC is not supported"
#elif defined(__wasm64__)
#    define LUDUS_TARGET_ARCH LUDUS_CPU_WASM64
#    define LUDUS_ARCH_WASM64 1
#elif defined(__wasm32__)
#    define LUDUS_TARGET_ARCH LUDUS_CPU_WASM32
#    define LUDUS_ARCH_WASM32 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define LUDUS_TARGET_ARCH LUDUS_CPU_ARM64
#    define LUDUS_ARCH_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64)
#    define LUDUS_TARGET_ARCH LUDUS_CPU_X86_64
#    define LUDUS_ARCH_X86_64 1
#else
#    error "Ludus target detection: unsupported CPU architecture"
#endif

#if LUDUS_TARGET_OS == LUDUS_OS_WEB
#    if LUDUS_TARGET_ARCH != LUDUS_CPU_WASM32 && LUDUS_TARGET_ARCH != LUDUS_CPU_WASM64
#        error "Ludus target detection: Emscripten requires a WebAssembly architecture"
#    endif
#elif LUDUS_TARGET_ARCH == LUDUS_CPU_WASM32 || LUDUS_TARGET_ARCH == LUDUS_CPU_WASM64
#    error "Ludus target detection: WebAssembly requires the Emscripten environment"
#endif

// Pointer width is a data-model fact, not an architecture guess (e.g. x32 ABI).
#if defined(__SIZEOF_POINTER__)
#    define LUDUS_POINTER_BITS (__SIZEOF_POINTER__ * 8)
#elif defined(_WIN64)
#    define LUDUS_POINTER_BITS 64
#elif defined(_WIN32)
#    define LUDUS_POINTER_BITS 32
#else
#    error "Ludus target detection: unknown pointer width"
#endif

#if LUDUS_POINTER_BITS != 32 && LUDUS_POINTER_BITS != 64
#    error "Ludus target detection: unsupported pointer width"
#endif

#define LUDUS_ENDIAN_LITTLE 1
#define LUDUS_ENDIAN_BIG 2
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#    if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#        define LUDUS_TARGET_ENDIAN LUDUS_ENDIAN_LITTLE
#    elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#        define LUDUS_TARGET_ENDIAN LUDUS_ENDIAN_BIG
#    else
#        error "Ludus target detection: unsupported byte order"
#    endif
#elif defined(_WIN32) || defined(__wasm32__) || defined(__wasm64__)
#    define LUDUS_TARGET_ENDIAN LUDUS_ENDIAN_LITTLE
#else
#    error "Ludus target detection: unknown byte order"
#endif

// -----------------------------------------------------------------------------
// Build flavor.
//
// The authoritative flavor is injected by the build system
// (cmake/EngineOptions.cmake defines LUDUS_BUILD_<FLAVOR>=1; the SDK-owned
// numeric LUDUS_BUILD_FLAVOR_ID arrives from the generated assert_config.hpp).
// The block below is only a fallback so a translation unit compiled outside the
// engine build (a standalone probe, a micro-benchmark) still classifies itself.
// -----------------------------------------------------------------------------
#if !defined(LUDUS_BUILD_DEBUG) && !defined(LUDUS_BUILD_DEVELOPMENT) && !defined(LUDUS_BUILD_PROFILE) &&               \
    !defined(LUDUS_BUILD_RELEASE)
#    if defined(_DEBUG) || defined(DEBUG)
#        define LUDUS_BUILD_DEBUG 1
#    elif defined(NDEBUG)
#        define LUDUS_BUILD_RELEASE 1
#    else
#        define LUDUS_BUILD_DEVELOPMENT 1
#    endif
#endif

// Exactly one flavor, with value 1. Presence is the legacy API contract.
#if (defined(LUDUS_BUILD_DEBUG) + defined(LUDUS_BUILD_DEVELOPMENT) + defined(LUDUS_BUILD_PROFILE) +                    \
     defined(LUDUS_BUILD_RELEASE)) != 1
#    error "Ludus build configuration: select exactly one build flavor"
#endif

#if defined(LUDUS_BUILD_DEBUG)
#    if LUDUS_BUILD_DEBUG != 1
#        error "Ludus build configuration: selected build flavor must equal 1"
#    endif
#endif

#if defined(LUDUS_BUILD_DEVELOPMENT)
#    if LUDUS_BUILD_DEVELOPMENT != 1
#        error "Ludus build configuration: selected build flavor must equal 1"
#    endif
#endif

#if defined(LUDUS_BUILD_PROFILE)
#    if LUDUS_BUILD_PROFILE != 1
#        error "Ludus build configuration: selected build flavor must equal 1"
#    endif
#endif

#if defined(LUDUS_BUILD_RELEASE)
#    if LUDUS_BUILD_RELEASE != 1
#        error "Ludus build configuration: selected build flavor must equal 1"
#    endif
#endif

#if defined(LUDUS_BUILD_DEBUG)
#    define LUDUS_IS_DEBUG_BUILD 1
#endif

#if defined(LUDUS_BUILD_DEVELOPMENT)
#    define LUDUS_IS_DEVELOPMENT_BUILD 1
#endif

#if defined(LUDUS_BUILD_PROFILE)
#    define LUDUS_IS_PROFILE_BUILD 1
#endif

#if defined(LUDUS_BUILD_RELEASE)
#    define LUDUS_IS_RELEASE_BUILD 1
#endif

#if defined(LUDUS_BUILD_SANITIZED)
#    if LUDUS_BUILD_SANITIZED != 1
#        error "Ludus build configuration: sanitizer marker must equal 1"
#    endif
#    define LUDUS_IS_SANITIZED_BUILD 1
#endif
