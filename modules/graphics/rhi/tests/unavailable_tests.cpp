#include <ludus/graphics/rhi/render.h>
#include <ludus/graphics/rhi/rhi.h>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::graphics::rhi;
TEST_CASE("Deferred Metal backend fails startup without exposing a usable session", "[rhi][macos]")
{
    Shutdown();
    CHECK(Start({}, {}) == StartStatus::Failed);
    CHECK(GetStartup().State == StartupState::Failed);
    CHECK(GetStartup().Error == StartupError::BackendUnavailable);
    CHECK(GetStartup().SelectedBackend == Backend::Metal);
    CHECK(BeginFrameStatus() == FrameStatus::NotReady);
    CHECK(EndFrameStatus() == FrameStatus::NotReady);
    UniformHandle uniform;
    CHECK(CreateUniform(16, uniform) == ResourceStatus::NotReady);
    CHECK(Start({}, {}) == StartStatus::Busy);
    Shutdown();
    Shutdown();
    CHECK(GetStartup().State == StartupState::Idle);
    CHECK(Start({}, {}) == StartStatus::Failed);
    Shutdown();
}
