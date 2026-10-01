#pragma once

#include <ludus/foundation/base/core.h>      // LUDUS_INLINE + foundational vocabulary
#include <ludus/foundation/base/pointer.hpp> // ludus::foundation::core::UniquePtr
#include <ludus/platform/config.h>
#include <ludus/platform/native_window.h>

#include <string> // owned strings are required for stable window names
#include <string_view>

namespace ludus::platform
{
namespace browser
{
struct WindowState;
struct InputEvent;
} // namespace browser

class WindowBase
{
public:
    struct CreateInfo final
    {
        std::string Name;
        ludus::foundation::uint32 Width = 800;
        ludus::foundation::uint32 Height = 600;
        // Browser only. Selector is copied at creation; the matching canvas is
        // borrowed and must remain in the DOM until this window is destroyed.
        std::string_view CanvasSelector = "#canvas";
        ludus::foundation::uint32 FramebufferLimit = 4096;
        bool CaptureBrowserInput = true;
    };

    struct Event final
    {
    };

public:
    virtual ~WindowBase() = default;
    virtual bool HandleEvent(const Event& event) noexcept = 0;

    // Browser snapshots/events are copied into caller-owned storage. Native
    // backends return false. Call HandleEvent once per animation frame to refresh
    // CSS size/DPR/DOM attachment; callbacks also update state between frames.
    virtual bool GetBrowserState(browser::WindowState&) const noexcept
    {
        return false;
    }
    virtual bool PollBrowserInput(browser::InputEvent&) noexcept
    {
        return false;
    }
    // Supply the actual device texture-dimension limit after GPU startup.
    virtual bool SetBrowserFramebufferLimit(ludus::foundation::uint32) noexcept
    {
        return false;
    }

    [[nodiscard]] LUDUS_INLINE NativeWindowInfo GetNativeWindowInfo() const noexcept
    {
        return mNativeWindowInfo;
    }

protected:
    WindowBase() = delete;
    LUDUS_INLINE explicit WindowBase(const CreateInfo& info) noexcept
        : mName(info.Name), mNativeWindowInfo{ .Width = info.Width, .Height = info.Height }
    {
    }

protected:
    std::string mName;
    NativeWindowInfo mNativeWindowInfo = {};
};

using Window = WindowBase;

class WindowManager final
{
public:
    struct InitializeInfo final
    {
    };

public:
    WindowManager() = default;
    ~WindowManager() = default;

    bool Initialize(const InitializeInfo& info) noexcept;
    bool CreateWindow(const WindowBase::CreateInfo& info,
                      ludus::foundation::core::UniquePtr<Window>& outWindow) noexcept;
};
} // namespace ludus::platform
