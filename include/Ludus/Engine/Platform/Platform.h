#pragma once

#include <cstdint>

namespace ludus::platform
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

// Always define a known set of macros, even if 0.
#if defined(_WIN32) || defined(_WIN64)
    #ifndef LUDUS_WINDOWS
        #define LUDUS_WINDOWS
        constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::WINDOWS;
    #endif
#endif  // defined(_WIN32) || defined(_WIN64)

#if defined(__linux__)
    #ifndef LUDUS_LINUX
        #define LUDUS_LINUX
        constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::LINUX;
    #endif
#endif  // defined(__linux__)

#if defined(__APPLE__)
    #ifndef LUDUS_MAC
        #define LUDUS_MAC
        constexpr const PlatformType CURRENT_PLATFORM_TYPE = PlatformType::MAC;
    #endif
#endif  // defined(__APPLE__)

#if !defined(LUDUS_DEBUG)
    #if !defined(NDEBUG)
        #define LUDUS_DEBUG
    #endif    // !defined(NDEBUG)
#endif

#if defined(NDEBUG)
    #ifndef LUDUS_RELEASE
        #define LUDUS_RELEASE
    #endif
#endif  // defined(NDEBUG)

#if defined(_MSC_VER)
    #define LUDUS_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define LUDUS_DEBUGBREAK() __builtin_trap()
#else
    #define LUDUS_DEBUGBREAK() ((void)0)
#endif
}   // namespace ludus::platform

#if defined(LUDUS_WINDOWS)
    #include <Ludus/Engine/Platform/Windows/Common.h>
#elif defined(LUDUS_LINUX)
    #include <Ludus/Engine/Platform/Unix/Common.h>
#endif  // defined(LUDUS_WINDOWS)