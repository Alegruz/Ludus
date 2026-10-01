#pragma once

#include <ludus/foundation/base/types.h>

struct wl_display;
struct wl_surface;

namespace ludus::platform
{
enum class WindowSystem : ludus::foundation::uint8
{
    Headless,
    Wayland,
    WebCanvas,
};

// Borrowed handles. The platform window must outlive its RHI connection.
struct NativeWindowInfo final
{
    WindowSystem System = WindowSystem::Headless;
    ::wl_display* Display = nullptr;
    ::wl_surface* Surface = nullptr;
    ludus::foundation::uint32 Width = 800;
    ludus::foundation::uint32 Height = 600;
    // WebCanvas: borrowed selector owned by the platform window. The matching
    // DOM canvas and window must both outlive the RHI connection. Keep the
    // selector identifying that same canvas until window destruction. No GPU handle.
    const char* CanvasSelector = nullptr;
};
} // namespace ludus::platform
