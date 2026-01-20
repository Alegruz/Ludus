#pragma once

#include <Ludus/Engine/Platform/Platform.h>

#if defined(LUDUS_WINDOWS)
    // Windows
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #define UNICODE
    #include <windows.h>
#endif  // defined(LUDUS_WINDOWS)