#pragma once

#include <cstdint>

// Platform detection and build configuration
// This is Core infrastructure needed throughout the codebase

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #ifndef LUDUS_WINDOWS
        #define LUDUS_WINDOWS
    #endif
#endif

#if defined(__linux__)
    #ifndef LUDUS_LINUX
        #define LUDUS_LINUX
    #endif
#endif

#if defined(__APPLE__)
    #ifndef LUDUS_MAC
        #define LUDUS_MAC
    #endif
#endif

// Build configuration
#if !defined(LUDUS_DEBUG)
    #if !defined(NDEBUG)
        #define LUDUS_DEBUG
    #endif
#endif

#if defined(NDEBUG)
    #ifndef LUDUS_RELEASE
        #define LUDUS_RELEASE
    #endif
#endif

namespace ludus::core
{
    enum class PlatformType : uint8_t
    {
        UNKNOWN,
        WINDOWS,
        LINUX,
        MAC,
        COUNT = MAC,
        DEFAULT = WINDOWS,
    };

#if defined(LUDUS_WINDOWS)
    constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::WINDOWS;
#elif defined(LUDUS_LINUX)
    constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::LINUX;
#elif defined(LUDUS_MAC)
    constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::MAC;
#else
    #error "Unsupported platform"
#endif
}  // namespace ludus::core
