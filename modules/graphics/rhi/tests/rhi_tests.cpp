// Include the public API before Volk to catch native-handle namespace mistakes.
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/graphics/rhi/rhi.h>
#include <ludus/platform/base/window.h>
#include <ludus/platform/native_window.h>

#include <cstdlib>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>
#include <volk.h>

using namespace ludus;

static_assert(std::is_same_v<decltype(graphics::rhi::WindowInfo::Display), ::wl_display*>);
static_assert(std::is_same_v<decltype(graphics::rhi::WindowInfo::Surface), ::wl_surface*>);

TEST_CASE("A window cannot connect before RHI initialization", "[rhi]")
{
    graphics::rhi::Shutdown();
    CHECK_FALSE(graphics::rhi::ConnectWindow({}));
    graphics::rhi::Shutdown();
}

TEST_CASE("Wayland surface and device lifecycle", "[rhi][wayland]")
{
    // Opt in on hosts with a running compositor and an eligible Vulkan GPU.
    if (std::getenv("LUDUS_TEST_WAYLAND") == nullptr)
    {
        SKIP("Set LUDUS_TEST_WAYLAND=1 to exercise the live Wayland/Vulkan lifecycle.");
    }
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    platform::WindowManager manager;
    foundation::UniquePtr<platform::Window> window;
    // Destroy Vulkan objects before the platform window even on assertion failure.
    struct ShutdownGuard
    {
        ~ShutdownGuard()
        {
            graphics::rhi::Shutdown();
        }
    } guard;

    REQUIRE(manager.Initialize({}));
    REQUIRE(manager.CreateWindow({.Name = "Ludus Wayland lifecycle test"}, window));
    const auto native = window->GetNativeWindowInfo();
    REQUIRE(native.System == platform::WindowSystem::Wayland);
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        REQUIRE(graphics::rhi::Initialize({.Name = "Wayland lifecycle test"}));
        REQUIRE(vkCreateWaylandSurfaceKHR != nullptr);
        CHECK_FALSE(graphics::rhi::ConnectWindow({}));
        REQUIRE(graphics::rhi::ConnectWindow(native));
        REQUIRE(graphics::rhi::ConnectWindow(native));
        auto invalid = native;
        invalid.Surface = nullptr;
        CHECK_FALSE(graphics::rhi::ConnectWindow(invalid));
        graphics::rhi::Shutdown();
        graphics::rhi::Shutdown();
    }
#else
    SKIP("Wayland was disabled at build time.");
#endif
}
