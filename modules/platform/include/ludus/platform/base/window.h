#pragma once

#include <ludus/platform/config.h>
#include <string> // owned strings are required for stable window names

namespace ludus::platform
{
class WindowBase
{
public:
    struct CreateInfo final
    {
        std::string Name;
    };

    struct Event final
    {
    };

public:
    virtual ~WindowBase() = default;
    virtual bool HandleEvent(const Event& event) noexcept = 0;

protected:
    WindowBase() = delete;
    LUDUS_INLINE explicit WindowBase(const CreateInfo& info) noexcept : mName(info.Name) {}

protected:
    std::string mName;
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
