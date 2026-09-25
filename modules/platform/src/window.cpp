#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>
#include <ludus/platform/config.h> // LUDUS_PLATFORM_WAYLAND / LUDUS_PLATFORM_HEADLESS

#include <utility>

#if defined(LUDUS_PLATFORM_WAYLAND)
#    include <ludus/platform/wayland/window.h>
#elif defined(LUDUS_PLATFORM_HEADLESS)
#    include <ludus/platform/headless/window.h>
#else
#    error "No platform windowing backend selected (expected LUDUS_PLATFORM_WAYLAND or LUDUS_PLATFORM_HEADLESS)"
#endif

namespace ludus::platform
{
bool WindowManager::Initialize(const InitializeInfo& info) noexcept
{
#if defined(LUDUS_PLATFORM_WAYLAND)
    return wayland::InitializeWayland(info);
#elif defined(LUDUS_PLATFORM_HEADLESS)
    return headless::InitializeHeadless(info);
#endif
}

bool WindowManager::CreateWindow(const WindowBase::CreateInfo& info,
                                 ludus::foundation::core::UniquePtr<Window>& outWindow) noexcept
{
#if defined(LUDUS_PLATFORM_WAYLAND)
    auto window = wayland::CreateWindow(info);
#elif defined(LUDUS_PLATFORM_HEADLESS)
    auto window = headless::CreateWindow(info);
#endif
    if (!window)
    {
        outWindow = nullptr;
        return false;
    }

    outWindow = std::move(window);
    return true;
}
} // namespace ludus::platform
