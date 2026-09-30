#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/platform/native_window.h>

#include <string>

namespace ludus::graphics::rhi
{
struct ApplicationInfo final
{
    std::string Name;
    ludus::foundation::uint32 Version = 0;
};

using WindowInfo = ludus::platform::NativeWindowInfo;

// Loads Vulkan entry points and creates an instance. Returns false on failure.
// Repeated calls succeed until Shutdown; serialize all lifecycle calls.
// Does not create a device or guarantee that a usable GPU is present.
[[nodiscard]] bool Initialize(const ApplicationInfo& appInfo) noexcept;
// Creates a surface and a device with graphics/presentation queues. Only one
// window may be connected until Shutdown; reconnecting the same window succeeds.
// The window and its display must remain alive until Shutdown. Headless returns false.
[[nodiscard]] bool ConnectWindow(const WindowInfo& windowInfo) noexcept;
[[nodiscard]] bool InitializeRendering() noexcept;
void ShutdownRendering() noexcept;
void Shutdown() noexcept;

[[nodiscard]] bool BeginFrame() noexcept;
[[nodiscard]] bool EndFrame() noexcept;
} // namespace ludus::graphics::rhi
