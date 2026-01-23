#pragma once

#if defined(LUDUS_WINDOWS)
    // Windows
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define UNICODE
    #include <windows.h>

// Enable detailed memory tracking in debug builds
#if defined(LUDUS_DEBUG)
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
#endif

namespace ludus::platform
{
    void PrintWin32Error() noexcept;
}   // namespace ludus::platform
#endif  // defined(LUDUS_WINDOWS)
