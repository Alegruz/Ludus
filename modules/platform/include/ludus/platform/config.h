#pragma once

// Platform-module configuration.
//
// OS *family* and CPU *architecture* detection (LUDUS_PLATFORM_WINDOWS/MACOS/
// LINUX/DESKTOP, LUDUS_ARCH_*) is a foundational concern and lives in
// <ludus/foundation/base/config.h>; this header re-exposes it so existing
// platform-module includes keep working, and adds only what is genuinely a
// platform-module concern: which windowing backend is active.
//
// The active windowing backend is selected by the build system, not inferred
// here: modules/platform/CMakeLists.txt defines LUDUS_PLATFORM_WAYLAND=1 when
// the Wayland backend is compiled, or LUDUS_PLATFORM_HEADLESS=1 when Wayland is
// unavailable and the null backend is used. Base config.h only classifies the
// OS so that consumers which merely need the OS family do not accidentally
// force a backend on.

#include <ludus/foundation/base/config.h>

#if defined(LUDUS_PLATFORM_WAYLAND)
#    define LUDUS_PLATFORM_HAS_WAYLAND 1
#endif

#if defined(LUDUS_PLATFORM_X11)
#    define LUDUS_PLATFORM_HAS_X11 1
#endif
