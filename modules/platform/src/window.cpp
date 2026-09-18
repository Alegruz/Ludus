#include <ludus/foundation/base/defines.h>
#include <ludus/foundation/base/pointer.hpp>

#include <memory>   // TODO: remove and define move

#include <ludus/platform/base/window.h>

#if defined(LUDUS_PLATFORM_WAYLAND)
#include <ludus/platform/wayland/window.h>
#else
#error "Unsupported platform"
#endif

namespace ludus::platform
{
    bool WindowManager::Initialize(const InitializeInfo& info) noexcept
    {
#if defined(LUDUS_PLATFORM_WAYLAND)
        return wayland::InitializeWayland(info);
#else
        return false;
#endif
    }

    bool WindowManager::CreateWindow(const WindowBase::CreateInfo& info, ludus::foundation::core::UniquePtr<Window>& outWindow) noexcept
    {
#if defined(LUDUS_PLATFORM_WAYLAND)
        auto window = wayland::CreateWindow(info);
        if (!window)
        {
            outWindow = nullptr;
            return false;
        }

        outWindow = std::move(window);
        return true;
#else
        outWindow = nullptr;
        return false;
#endif
    }
}
