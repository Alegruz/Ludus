#include <ludus/foundation/base/pointer.hpp>
#include <ludus/platform/browser/window.h>
#include <ludus/platform/headless/window.h>
#include <ludus/platform/native_window.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Headless windows preserve native descriptors and reject browser controls")
{
    ludus::platform::Window::CreateInfo info;
    info.Width = 321;
    info.Height = 123;
    auto window = ludus::platform::headless::CreateWindow(info);
    REQUIRE(window);
    const auto native = window->GetNativeWindowInfo();
    REQUIRE(native.System == ludus::platform::WindowSystem::Headless);
    REQUIRE(native.Width == 321);
    REQUIRE(native.Height == 123);
    REQUIRE(native.CanvasSelector == nullptr);
    REQUIRE_FALSE(window->HandleEvent({}));
    ludus::platform::browser::WindowState state;
    ludus::platform::browser::InputEvent event;
    REQUIRE_FALSE(window->GetBrowserState(state));
    REQUIRE_FALSE(window->PollBrowserInput(event));
    REQUIRE_FALSE(window->SetBrowserFramebufferLimit(128));
    window.Reset();
    REQUIRE_FALSE(window);
}
