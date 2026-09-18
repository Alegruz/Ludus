#pragma once

#if defined(_WIN32)
    #define LUDUS_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define LUDUS_PLATFORM_MACOS 1
#elif defined(__linux__)
    #define LUDUS_PLATFORM_LINUX 1
#endif

#if defined(__linux__) && !defined(LUDUS_PLATFORM_X11) && !defined(LUDUS_PLATFORM_WAYLAND)
    #define LUDUS_PLATFORM_WAYLAND 1
#endif

#if defined(LUDUS_PLATFORM_WINDOWS) || defined(LUDUS_PLATFORM_MACOS) || defined(LUDUS_PLATFORM_LINUX)
    #define LUDUS_PLATFORM_DESKTOP 1
#endif

#if defined(LUDUS_PLATFORM_WAYLAND)
    #define LUDUS_PLATFORM_HAS_WAYLAND 1
#endif

#if defined(LUDUS_PLATFORM_X11)
    #define LUDUS_PLATFORM_HAS_X11 1
#endif
