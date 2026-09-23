#include <new> // std::nothrow

#include <ludus/foundation/base/defines.h>
#include <ludus/foundation/base/pointer.hpp>

#include <ludus/platform/base/window.h>
#include <ludus/platform/headless/window.h>

namespace ludus::platform::headless
{
bool InitializeHeadless([[maybe_unused]] const WindowManager::InitializeInfo& info) noexcept
{
    // Nothing to connect to; the headless backend is always "ready".
    return true;
}

UniquePtr<WindowHeadless> CreateWindow(const Window::CreateInfo& info) noexcept
{
    WindowHeadless* window = new (std::nothrow) WindowHeadless(info); // TODO: custom allocator
    return UniquePtr<WindowHeadless>(window);
}

bool WindowHeadless::HandleEvent([[maybe_unused]] const Event& event) noexcept
{
    // No display and no event source: report that there is nothing to
    // process so a caller's main loop terminates immediately.
    return false;
}
} // namespace ludus::platform::headless
