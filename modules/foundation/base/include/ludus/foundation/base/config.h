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

// -----------------------------------------------------------------------------
// Operating-system family (detection only).
// -----------------------------------------------------------------------------
#if defined(_WIN32)
#    define LUDUS_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#    define LUDUS_PLATFORM_MACOS 1
#elif defined(__linux__)
#    define LUDUS_PLATFORM_LINUX 1
#endif

#if defined(LUDUS_PLATFORM_WINDOWS) || defined(LUDUS_PLATFORM_MACOS) || defined(LUDUS_PLATFORM_LINUX)
#    define LUDUS_PLATFORM_DESKTOP 1
#endif

// -----------------------------------------------------------------------------
// CPU architecture (detection only). Needed for SIMD/math/atomics decisions
// that differ across the backends Ludus targets next.
// -----------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
#    define LUDUS_ARCH_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define LUDUS_ARCH_ARM64 1
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
#    define LUDUS_IS_SANITIZED_BUILD 1
#endif
