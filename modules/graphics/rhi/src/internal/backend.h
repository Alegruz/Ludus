#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/rhi.h>

namespace ludus::graphics::rhi::backend
{
Backend Kind() noexcept;
StartupError Start(const ApplicationInfo&, const WindowInfo&, ludus::foundation::uint32 token) noexcept;
bool Initialize(const ApplicationInfo&) noexcept;
bool ConnectWindow(const WindowInfo&) noexcept;
bool InitializeRendering() noexcept;
void ShutdownRendering() noexcept;
void Shutdown() noexcept;
bool BeginFrame() noexcept;
bool EndFrame() noexcept;
FrameStatus Begin() noexcept;
FrameStatus End() noexcept;
} // namespace ludus::graphics::rhi::backend
