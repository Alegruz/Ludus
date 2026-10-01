#pragma once

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/base/window.h>

namespace ludus::platform::browser
{
bool InitializeBrowser(const WindowManager::InitializeInfo&) noexcept;
ludus::foundation::UniquePtr<Window> CreateWindow(const Window::CreateInfo& info) noexcept;
} // namespace ludus::platform::browser
