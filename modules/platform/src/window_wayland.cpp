#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
// wayland-scanner emits the protocol marshalling code as a .cpp that must be
// compiled directly into this translation unit; the include is intentional.
#include <xdg-shell-protocol.cpp> // NOLINT(bugprone-suspicious-include)

#include <cstring> // TODO: remove all c libraries
#include <new>     // std::nothrow

#include <ludus/foundation/base/defines.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/logging/log.hpp>

#include <ludus/platform/base/window.h>
#include <ludus/platform/log_categories.h>
#include <ludus/platform/wayland/window.h>

namespace ludus::foundation::core
{
template <>
struct DefaultDeleter<wl_display>
{
    LUDUS_INLINE void operator()(wl_display* ptr) const noexcept
    {
        if (ptr)
        {
            wl_display_disconnect(ptr);
        }
    }
};

template <>
struct DefaultDeleter<wl_registry>
{
    LUDUS_INLINE void operator()(wl_registry* ptr) const noexcept
    {
        if (ptr)
        {
            wl_registry_destroy(ptr);
        }
    }
};

template <>
struct DefaultDeleter<wl_compositor>
{
    LUDUS_INLINE void operator()([[maybe_unused]] wl_compositor* ptr) const noexcept {}
};

template <>
struct DefaultDeleter<xdg_wm_base>
{
    LUDUS_INLINE void operator()(xdg_wm_base* ptr) const noexcept
    {
        if (ptr)
        {
            xdg_wm_base_destroy(ptr);
        }
    }
};

void DefaultDeleter<wl_surface>::operator()(wl_surface* ptr) const noexcept
{
    if (ptr)
    {
        wl_surface_destroy(ptr);
    }
}

void DefaultDeleter<xdg_surface>::operator()(xdg_surface* ptr) const noexcept
{
    if (ptr)
    {
        xdg_surface_destroy(ptr);
    }
}

void DefaultDeleter<xdg_toplevel>::operator()(xdg_toplevel* ptr) const noexcept
{
    if (ptr)
    {
        xdg_toplevel_destroy(ptr);
    }
}
} // namespace ludus::foundation::core

namespace ludus::platform::wayland
{
static void
onGlobalRegistry(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) noexcept;
static void onGlobalRegistryRemove(void* data, wl_registry* registry, uint32_t name) noexcept;
static void onXdgWmBasePing(void* data, xdg_wm_base* xdgWmBase, uint32_t serial) noexcept;
static void onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial) noexcept;
static void
onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states) noexcept;
static void onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel) noexcept;
static void onXdgToplevelConfigureBounds(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height) noexcept;
static void onXdgToplevelWmCapabilities(void* data, xdg_toplevel* xdgToplevel, wl_array* capabilities) noexcept;

static UniquePtr<wl_display> gDisplay = nullptr;
static UniquePtr<wl_registry> gRegistry = nullptr;
static UniquePtr<wl_compositor> gCompositor = nullptr;
static wl_registry_listener gRegistryListener = {.global = onGlobalRegistry, .global_remove = onGlobalRegistryRemove};
static UniquePtr<xdg_wm_base> gXdgWmBase = nullptr;
static xdg_wm_base_listener gXdgWmBaseListener = {.ping = onXdgWmBasePing};
static xdg_surface_listener gXdgSurfaceListener = {.configure = onXdgSurfaceConfigure};
static xdg_toplevel_listener gXdgToplevelListener = {.configure = onXdgToplevelConfigure,
                                                     .close = onXdgToplevelClose,
                                                     .configure_bounds = onXdgToplevelConfigureBounds,
                                                     .wm_capabilities = onXdgToplevelWmCapabilities};

bool InitializeWayland([[maybe_unused]] const WindowManager::InitializeInfo& info) noexcept
{
    wl_display* display = gDisplay.Get();
    if (display == nullptr)
    {
        display = wl_display_connect(nullptr);
    }

    if (display == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to connect to Wayland display");
        return false;
    }

    wl_registry* registry = gRegistry.Get();
    if (registry == nullptr)
    {
        registry = wl_display_get_registry(display);
    }

    if (registry == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to get Wayland registry");
        return false;
    }

    int result = wl_registry_add_listener(registry, &gRegistryListener, nullptr);
    if (result != 0)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to add Wayland registry listener. Result: {}", result);
        return false;
    }

    result = wl_display_roundtrip(display);
    if (result < 0)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to perform Wayland display roundtrip. Result: {}", result);
        return false;
    }

    gDisplay = UniquePtr<wl_display>(display);
    gRegistry = UniquePtr<wl_registry>(registry);

    return true;
}

void onGlobalRegistry([[maybe_unused]] void* data,
                      [[maybe_unused]] wl_registry* registry,
                      uint32_t name,
                      const char* interface,
                      uint32_t version) noexcept
{
    if (std::strcmp(interface, wl_compositor_interface.name) == 0)
    {
        wl_compositor* compositor =
            static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version));
        if (compositor == nullptr)
        {
            LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to bind Wayland compositor");
            return;
        }
        gCompositor = UniquePtr<wl_compositor>(compositor);
    }
    if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        xdg_wm_base* xdgWmBase =
            static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, version));
        if (xdgWmBase == nullptr)
        {
            LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to bind XDG WM Base");
            return;
        }
        xdg_wm_base_add_listener(xdgWmBase, &gXdgWmBaseListener, nullptr);
        gXdgWmBase = UniquePtr<xdg_wm_base>(xdgWmBase);
    }

    LUDUS_LOG_TRACE(LOG_PLATFORM, "Wayland global: name={}, interface={}, version={}", name, interface, version);
}

// `name` is only referenced by a LUDUS_LOG_TRACE call, which compiles out below
// the Trace level, so mark it maybe_unused to stay warning-clean in every build.
void onGlobalRegistryRemove([[maybe_unused]] void* data,
                            [[maybe_unused]] wl_registry* registry,
                            [[maybe_unused]] uint32_t name) noexcept
{
    LUDUS_LOG_TRACE(LOG_PLATFORM, "Wayland global removed: name={}", name);
}

void onXdgWmBasePing([[maybe_unused]] void* data, [[maybe_unused]] xdg_wm_base* xdgWmBase, uint32_t serial) noexcept
{
    xdg_wm_base_pong(xdgWmBase, serial);
}

UniquePtr<WindowWayland> CreateWindow(const Window::CreateInfo& info) noexcept
{
    WindowWayland::CreateInfo createInfo{
        .BaseCreateInfo = info,
        .Surface = nullptr,
        .XdgSurface = nullptr,
        .XdgToplevel = nullptr,
    };

    wl_surface* surface = wl_compositor_create_surface(gCompositor.Get());
    if (surface == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to create Wayland surface");
        return nullptr;
    }

    xdg_surface* xdgSurface = xdg_wm_base_get_xdg_surface(gXdgWmBase.Get(), surface);
    if (xdgSurface == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to create XDG surface");
        wl_surface_destroy(surface);
        return nullptr;
    }

    xdg_toplevel* xdgToplevel = xdg_surface_get_toplevel(xdgSurface);
    if (xdgToplevel == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to create XDG toplevel");
        xdg_surface_destroy(xdgSurface);
        wl_surface_destroy(surface);
        return nullptr;
    }
    xdg_toplevel_set_title(xdgToplevel, info.Name.c_str());
    xdg_toplevel_set_app_id(xdgToplevel, "ludus");
    xdg_toplevel_set_min_size(xdgToplevel, 320, 200);
    xdg_toplevel_set_max_size(xdgToplevel, 0, 0);

    createInfo.Surface = UniquePtr<::wl_surface>(surface);
    createInfo.XdgSurface = UniquePtr<xdg_surface>(xdgSurface);
    createInfo.XdgToplevel = UniquePtr<xdg_toplevel>(xdgToplevel);

    // Engine code is exception-free (-fno-exceptions); use the non-throwing
    // new and null-check rather than relying on a bad_alloc exception.
    WindowWayland* window = new (std::nothrow) WindowWayland(std::move(createInfo)); // TODO: custom allocator
    if (window == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_PLATFORM, "Failed to allocate Wayland window");
        return nullptr;
    }
    xdg_surface_add_listener(window->mXdgSurface.Get(), &gXdgSurfaceListener, window);
    xdg_toplevel_add_listener(window->mXdgToplevel.Get(), &gXdgToplevelListener, window);

    wl_surface_commit(window->mSurface.Get());
    wl_display_roundtrip(gDisplay.Get());

    return UniquePtr<WindowWayland>(window);
}

void onXdgSurfaceConfigure(void* data, [[maybe_unused]] xdg_surface* xdgSurface, uint32_t serial) noexcept
{
    WindowWayland* window = static_cast<WindowWayland*>(data);
    if (window == nullptr)
    {
        return;
    }

    xdg_surface_ack_configure(xdgSurface, serial);
}

// Signature is dictated by the Wayland xdg_toplevel listener; the adjacent
// width/height parameters cannot be reordered.
// Signature is dictated by the Wayland xdg_toplevel listener; the adjacent
// width/height parameters cannot be reordered. The suppression must sit on the
// parameter lines themselves (a leading NOLINTNEXTLINE does not cover a
// multi-line signature, since the diagnostic points at the parameters).
void onXdgToplevelConfigure(void* data,
                            [[maybe_unused]] xdg_toplevel* xdgToplevel,
                            [[maybe_unused]] int32_t width, // NOLINT(bugprone-easily-swappable-parameters)
                            [[maybe_unused]] int32_t height,
                            [[maybe_unused]] wl_array* states) noexcept
{
    WindowWayland* window = static_cast<WindowWayland*>(data);
    if (window == nullptr)
    {
        return;
    }

    window->SetClosed(false);
}

void onXdgToplevelClose(void* data, [[maybe_unused]] xdg_toplevel* xdgToplevel) noexcept
{
    WindowWayland* window = static_cast<WindowWayland*>(data);
    if (window == nullptr)
    {
        return;
    }

    window->SetClosed(true);
}

// Signature is dictated by the Wayland xdg_toplevel listener; the adjacent
// width/height parameters cannot be reordered.
void onXdgToplevelConfigureBounds([[maybe_unused]] void* data,
                                  [[maybe_unused]] xdg_toplevel* xdgToplevel,
                                  [[maybe_unused]] int32_t width, // NOLINT(bugprone-easily-swappable-parameters)
                                  [[maybe_unused]] int32_t height) noexcept
{
}

void onXdgToplevelWmCapabilities([[maybe_unused]] void* data,
                                 [[maybe_unused]] xdg_toplevel* xdgToplevel,
                                 [[maybe_unused]] wl_array* capabilities) noexcept
{
}

bool WindowWayland::HandleEvent([[maybe_unused]] const Event& event) noexcept
{
    if (IsClosed())
    {
        return false;
    }

    const int result = wl_display_dispatch(gDisplay.Get());
    if (result < 0)
    {
        return false;
    }

    return !IsClosed();
}
} // namespace ludus::platform::wayland
