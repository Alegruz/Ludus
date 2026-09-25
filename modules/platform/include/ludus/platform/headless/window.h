#pragma once

#include <ludus/foundation/base/core.h> // LUDUS_INLINE + foundational vocabulary
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>
#include <ludus/platform/config.h>

namespace ludus::platform::headless
{
using ludus::foundation::UniquePtr;

class WindowHeadless;

// Headless (null) windowing backend used when no real backend (e.g. Wayland)
// is available at build time. It lets the platform module compile and link
// on headless CI runners and hosts without a display server. Initialization
// succeeds; created windows report no events so a main loop exits promptly.
bool InitializeHeadless(const WindowManager::InitializeInfo& info) noexcept;
UniquePtr<WindowHeadless> CreateWindow(const Window::CreateInfo& info) noexcept;

class WindowHeadless final : public WindowBase
{
public:
    friend UniquePtr<WindowHeadless> CreateWindow(const Window::CreateInfo& info) noexcept;

public:
    // No display to pump; report "no more events" so callers stop cleanly.
    bool HandleEvent(const Event& event) noexcept override;

private:
    LUDUS_INLINE explicit WindowHeadless(const WindowBase::CreateInfo& info) noexcept : WindowBase(info) {}
};
} // namespace ludus::platform::headless
