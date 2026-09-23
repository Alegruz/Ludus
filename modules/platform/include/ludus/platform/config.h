#pragma once

#if defined(_WIN32)
#    define LUDUS_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#    define LUDUS_PLATFORM_MACOS 1
#elif defined(__linux__)
#    define LUDUS_PLATFORM_LINUX 1
#endif

// The active windowing backend is selected by the build system, not inferred
// here: modules/platform/CMakeLists.txt defines LUDUS_PLATFORM_WAYLAND=1 when
// the Wayland backend is compiled, or LUDUS_PLATFORM_HEADLESS=1 when Wayland is
// unavailable and the null backend is used. config.h only classifies the OS so
// that consumers which merely need the OS family do not accidentally force a
// backend on.

#if defined(LUDUS_PLATFORM_WINDOWS) || defined(LUDUS_PLATFORM_MACOS) || defined(LUDUS_PLATFORM_LINUX)
#    define LUDUS_PLATFORM_DESKTOP 1
#endif

#if defined(LUDUS_PLATFORM_WAYLAND)
#    define LUDUS_PLATFORM_HAS_WAYLAND 1
#endif

#if defined(LUDUS_PLATFORM_X11)
#    define LUDUS_PLATFORM_HAS_X11 1
#endif
