#pragma once

#include <Ludus/Engine/Core/PlatformDetection.h>

namespace ludus::platform
{
    // Re-export Core's PlatformType for backwards compatibility
    using PlatformType = core::PlatformType;
    using core::CURRENT_PLATFORM_TYPE;

    [[nodiscard]] bool PreflightPlatformOrNotify() noexcept;
}   // namespace ludus::platform

#if defined(LUDUS_WINDOWS)
    #include <Ludus/Engine/Platform/Windows/Common.h>
#elif defined(LUDUS_LINUX)
    #include <Ludus/Engine/Platform/Unix/Common.h>
#elif defined(LUDUS_MAC)
    #include <Ludus/Engine/Platform/MacOs/Common.h>
#else
    #error "Unsupported platform"
#endif

namespace ludus::platform
{
#if defined(LUDUS_WINDOWS)
    using LudusInstance = HINSTANCE;
    using LudusWindowHandle = HWND;
#elif defined(LUDUS_LINUX)
    using LudusInstance = void*;
    using LudusWindowHandle = void*;
#elif defined(LUDUS_MAC)
    using LudusInstance = void*;
    using LudusWindowHandle = void*;
#else
    #error "Unsupported platform"
#endif
}
