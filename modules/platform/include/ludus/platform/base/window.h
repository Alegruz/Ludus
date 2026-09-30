#pragma once

#include <ludus/foundation/base/core.h>      // LUDUS_INLINE + foundational vocabulary
#include <ludus/foundation/base/pointer.hpp> // ludus::foundation::core::UniquePtr
#include <ludus/platform/config.h>
#include <ludus/platform/native_window.h>

#include <string> // owned strings are required for stable window names

namespace ludus::platform
{
class WindowBase
{
public:
    struct CreateInfo final
    {
        std::string Name;
        ludus::foundation::uint32 Width = 800;
        ludus::foundation::uint32 Height = 600;
    };

    struct Event final
    {
    };

public:
    virtual ~WindowBase() = default;
    virtual bool HandleEvent(const Event& event) noexcept = 0;

    [[nodiscard]] LUDUS_INLINE NativeWindowInfo GetNativeWindowInfo() const noexcept
    {
        return mNativeWindowInfo;
    }

protected:
    WindowBase() = delete;
    LUDUS_INLINE explicit WindowBase(const CreateInfo& info) noexcept
        : mName(info.Name), mNativeWindowInfo
        {
            .Width = info.Width,
            .Height = info.Height,
        }
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
