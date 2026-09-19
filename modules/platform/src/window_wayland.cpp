#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-shell-protocol.cpp>

#include <iostream> // TODO: replace with custom logger
#include <cstring>  // TODO: remove all c libraries

#include <ludus/foundation/base/defines.h>
#include <ludus/foundation/base/pointer.hpp>

#include <ludus/platform/base/window.h>
#include <ludus/platform/wayland/window.h>

namespace ludus::foundation::core
{
    template<>
    struct DefaultDeleter<wl_display>
    {
        LUDUS_INLINE void operator()(wl_display* ptr) const noexcept { if (ptr) { wl_display_disconnect(ptr); } }
    };

    template<>
    struct DefaultDeleter<wl_registry>
    {
        LUDUS_INLINE void operator()(wl_registry* ptr) const noexcept { if (ptr) { wl_registry_destroy(ptr); } }
    };

    template<>
    struct DefaultDeleter<wl_compositor>
    {
        LUDUS_INLINE void operator()([[maybe_unused]] wl_compositor* ptr) const noexcept {}
    };

    template<>
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
}

namespace ludus::platform::wayland
{
    static void onGlobalRegistry(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) noexcept;
    static void onGlobalRegistryRemove(void* data, wl_registry* registry, uint32_t name) noexcept;
    static void onXdgWmBasePing(void* data, xdg_wm_base* xdgWmBase, uint32_t serial) noexcept;
    static void onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial) noexcept;
    static void onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states) noexcept;
    static void onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel) noexcept;
    static void onXdgToplevelConfigureBounds(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height) noexcept;
    static void onXdgToplevelWmCapabilities(void* data, xdg_toplevel* xdgToplevel, wl_array* capabilities) noexcept;

    static UniquePtr<wl_display> gDisplay = nullptr;
    static UniquePtr<wl_registry> gRegistry = nullptr;
    static UniquePtr<wl_compositor> gCompositor = nullptr;
    static wl_registry_listener gRegistryListener =
    {
        .global = onGlobalRegistry,
        .global_remove = onGlobalRegistryRemove
    };
    static UniquePtr<xdg_wm_base> gXdgWmBase = nullptr;
    static xdg_wm_base_listener gXdgWmBaseListener =
    {
        .ping = onXdgWmBasePing
    };
    static xdg_surface_listener gXdgSurfaceListener =
    {
        .configure = onXdgSurfaceConfigure
    };
    static xdg_toplevel_listener gXdgToplevelListener =
    {
        .configure = onXdgToplevelConfigure,
        .close = onXdgToplevelClose,
        .configure_bounds = onXdgToplevelConfigureBounds,
        .wm_capabilities = onXdgToplevelWmCapabilities
    };

    bool InitializeWayland([[maybe_unused]] const WindowManager::InitializeInfo& info) noexcept
    {
        wl_display* display = gDisplay.Get();
        if (display == nullptr)
        {
            display = wl_display_connect(nullptr);
        }

        if (display == nullptr)
        {
            std::cerr << "Failed to connect to Wayland display" << std::endl;
            return false;
        }

        wl_registry* registry = gRegistry.Get();
        if (registry == nullptr)
        {
            registry = wl_display_get_registry(display);
        }

        if(registry == nullptr)
        {
            std::cerr << "Failed to get Wayland registry" << std::endl;
            return false;
        }

        int result = wl_registry_add_listener(registry, &gRegistryListener, nullptr);
        if (result != 0)
        {
            std::cerr << "Failed to add Wayland registry listener. Result: " << result << std::endl;
            return false;
        }

        result = wl_display_roundtrip(display);
        if (result < 0)
        {
            std::cerr << "Failed to perform Wayland display roundtrip. Result: " << result << std::endl;
            return false;
        }

        gDisplay = UniquePtr<wl_display>(display);
        gRegistry = UniquePtr<wl_registry>(registry);

        return true;
    }

    void onGlobalRegistry([[maybe_unused]] void* data, [[maybe_unused]] wl_registry* registry, uint32_t name, const char* interface, uint32_t version) noexcept
    {
        if (std::strcmp(interface, wl_compositor_interface.name) == 0)
        {
            wl_compositor* compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version));
            if(compositor == nullptr)
            {
                std::cerr << "Failed to bind Wayland compositor" << std::endl;
                return;
            }
            gCompositor = UniquePtr<wl_compositor>(compositor);
        }
        if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
        {
            xdg_wm_base* xdgWmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, version));
            if(xdgWmBase == nullptr)
            {
                std::cerr << "Failed to bind XDG WM Base" << std::endl;
                return;
            }
            xdg_wm_base_add_listener(xdgWmBase, &gXdgWmBaseListener, nullptr);
            gXdgWmBase = UniquePtr<xdg_wm_base>(xdgWmBase);
        }

#if defined(LUDUS_BUILD_DEBUG)
        std::cout << "Global registry event: name=" << name << ", interface=" << interface << ", version=" << version << std::endl;
#endif  // defined(LUDUS_BUILD_DEBUG)
    }

    void onGlobalRegistryRemove([[maybe_unused]] void* data, [[maybe_unused]] wl_registry* registry, uint32_t name) noexcept
    {
#if defined(LUDUS_BUILD_DEBUG)
        std::cout << "Global registry remove event: name=" << name << std::endl;
#endif  // defined(LUDUS_BUILD_DEBUG)
    }

    void onXdgWmBasePing([[maybe_unused]] void* data, [[maybe_unused]] xdg_wm_base* xdgWmBase, uint32_t serial) noexcept
    {
        xdg_wm_base_pong(xdgWmBase, serial);
    }

    UniquePtr<WindowWayland> CreateWindow(const Window::CreateInfo& info) noexcept
    {
        WindowWayland::CreateInfo createInfo
        {
            .BaseCreateInfo = info,
            .Surface = nullptr,
            .XdgSurface = nullptr,
            .XdgToplevel = nullptr,
        };

        wl_surface* surface = wl_compositor_create_surface(gCompositor.Get());
        if (surface == nullptr)
        {
            std::cerr << "Failed to create Wayland surface" << std::endl;
            return nullptr;
        }

        xdg_surface* xdgSurface = xdg_wm_base_get_xdg_surface(gXdgWmBase.Get(), surface);
        if (xdgSurface == nullptr)
        {
            std::cerr << "Failed to create XDG surface" << std::endl;
            wl_surface_destroy(surface);
            return nullptr;
        }

        xdg_toplevel* xdgToplevel = xdg_surface_get_toplevel(xdgSurface);
        if (xdgToplevel == nullptr)
        {
            std::cerr << "Failed to create XDG toplevel" << std::endl;
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

        WindowWayland* window = new WindowWayland(std::move(createInfo));   // TODO: custom allocator
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

    void onXdgToplevelConfigure(void* data, [[maybe_unused]] xdg_toplevel* xdgToplevel, [[maybe_unused]] int32_t width, [[maybe_unused]] int32_t height, [[maybe_unused]] wl_array* states) noexcept
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

    void onXdgToplevelConfigureBounds([[maybe_unused]] void* data, [[maybe_unused]] xdg_toplevel* xdgToplevel, [[maybe_unused]] int32_t width, [[maybe_unused]] int32_t height) noexcept
    {
    }

    void onXdgToplevelWmCapabilities([[maybe_unused]] void* data, [[maybe_unused]] xdg_toplevel* xdgToplevel, [[maybe_unused]] wl_array* capabilities) noexcept
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
