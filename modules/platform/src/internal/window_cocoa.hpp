#pragma once

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>

namespace ludus::platform::cocoa
{
bool OnMainThread() noexcept;
bool Initialize(const WindowManager::InitializeInfo& info) noexcept;
foundation::UniquePtr<Window> CreateWindow(const Window::CreateInfo& info) noexcept;
} // namespace ludus::platform::cocoa
