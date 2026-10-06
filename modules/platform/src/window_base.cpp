#include <ludus/platform/base/window.h>

namespace ludus::platform
{
WindowBase::WindowBase(const CreateInfo& info) noexcept
    : mName(info.Name), mNativeWindowInfo{ .Width = info.Width, .Height = info.Height }
{
}
} // namespace ludus::platform
