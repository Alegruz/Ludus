#pragma once

#include <ludus/platform/config.h>

#if !defined(LUDUS_PLATFORM_WAYLAND)
#    error "Wayland support requires LUDUS_PLATFORM_WAYLAND to be enabled."
#endif

#include <ludus/foundation/base/core.h> // LUDUS_INLINE + foundational vocabulary
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>

#include <utility>

struct wl_surface;
struct wl_array;
struct xdg_surface;
struct xdg_toplevel;

namespace ludus::foundation::core
{
template <>
struct DefaultDeleter<wl_surface>
{
    void operator()(wl_surface* ptr) const noexcept;
};

template <>
struct DefaultDeleter<xdg_surface>
{
    void operator()(xdg_surface* ptr) const noexcept;
};

template <>
struct DefaultDeleter<xdg_toplevel>
{
    void operator()(xdg_toplevel* ptr) const noexcept;
};
} // namespace ludus::foundation::core

namespace ludus::platform::wayland
{
using ludus::foundation::UniquePtr;

class WindowWayland;

bool InitializeWayland(const WindowManager::InitializeInfo& info) noexcept;
UniquePtr<WindowWayland> CreateWindow(const Window::CreateInfo& info) noexcept;

class WindowWayland final : public WindowBase
{
public:
    friend UniquePtr<WindowWayland> CreateWindow(const Window::CreateInfo& info) noexcept;

public:
    bool HandleEvent(const Event& event) noexcept override;
    [[nodiscard]] bool IsClosed() const noexcept
    {
        return mIsClosed;
    }
    void SetClosed(bool value) noexcept
    {
        mIsClosed = value;
    }

private:
    struct CreateInfo final
    {
        WindowBase::CreateInfo BaseCreateInfo;
        UniquePtr<wl_surface> Surface;
        UniquePtr<xdg_surface> XdgSurface;
        UniquePtr<xdg_toplevel> XdgToplevel;
    };

private:
    LUDUS_INLINE explicit WindowWayland(CreateInfo&& info) noexcept
        : WindowBase(info.BaseCreateInfo), mSurface(std::move(info.Surface)), mXdgSurface(std::move(info.XdgSurface)),
          mXdgToplevel(std::move(info.XdgToplevel))
    {
    }

private:
    UniquePtr<wl_surface> mSurface;
    UniquePtr<xdg_surface> mXdgSurface;
    UniquePtr<xdg_toplevel> mXdgToplevel;
    bool mIsClosed = false;
};
} // namespace ludus::platform::wayland
