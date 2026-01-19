#pragma once

#include <Ludus/Engine/Core/Common.h>

// Always define a known set of macros, even if 0.
#if defined(_WIN32) || defined(_WIN64)
    #ifndef LUDUS_WINDOWS
        #define LUDUS_WINDOWS
    #endif
#endif  // defined(_WIN32) || defined(_WIN64)

#if defined(__linux__)
    #ifndef LUDUS_LINUX
        #define LUDUS_LINUX
    #endif
#endif  // defined(__linux__)

#if defined(__APPLE__)
    #ifndef LUDUS_MAC
        #define LUDUS_MAC
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
