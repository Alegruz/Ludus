#pragma once

#if defined(LUDUS_WINDOWS)
    // Windows
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define UNICODE
    #include <windows.h>

namespace ludus::platform
{
    void PrintWin32Error() noexcept;
}   // namespace ludus::platform
#endif  // defined(LUDUS_WINDOWS)
