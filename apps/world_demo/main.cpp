#include "internal/application.h"
#include <charconv>
#include <ludus/foundation/base/config.h>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/graphics/rhi/rhi.h>
#include <string_view>
#if LUDUS_TARGET_OS == LUDUS_OS_WEB
#    include <emscripten.h>
namespace
{
ludus::world_demo::Application gApplication;
// clang-format off
EM_JS(void, Present, (ludus::foundation::uint32 state, ludus::foundation::uint32 mode, ludus::foundation::uint32 backend, ludus::foundation::uint32 tick, ludus::foundation::float32 x, ludus::foundation::float32 y), {
    const status = document.getElementById('status');
    status.dataset.state = ['stopped','loading','playing','failed'][state];
    status.dataset.mode = ['playing','paused','faulted'][mode];
    status.dataset.backend = ['vulkan','webgpu','webgl2'][backend];
    status.dataset.tick = tick; status.dataset.x = x; status.dataset.y = y;
    status.textContent = state == 3 ? 'Graphics could not start. Reload to retry.' : mode == 2 ? 'Simulation stopped. Press R to restart.' : mode == 1 ? 'Paused. N steps one tick; P resumes.' : 'WASD moves. Space attacks. P pauses. R restarts.';
});
// clang-format on
void Frame() noexcept
{
    const auto state = gApplication.Frame();
    const auto& session = gApplication.GetSession();
    const auto player = session.World().FindAuthored("player-start");
    const auto* transform = session.World().Transforms().Find(player);
    Present(static_cast<ludus::foundation::uint32>(state),
            static_cast<ludus::foundation::uint32>(session.Driver.GetMode()),
            static_cast<ludus::foundation::uint32>(ludus::graphics::rhi::GetStartup().SelectedBackend),
            static_cast<ludus::foundation::uint32>(session.World().GetTick()),
            transform != nullptr ? transform->Current.X : 0,
            transform != nullptr ? transform->Current.Y : 0);
}
} // namespace
#endif
int main(int argc, char** argv)
{
    using namespace ludus::world_demo;
    ludus::foundation::logging::LogConfig config;
    config.EnableConsole = true;
    config.EnableFile = false;
    ludus::foundation::logging::LogSystem::Initialize(config);
    if (argc > 1 && std::string_view(argv[1]) == "--headless")
    {
        Session session;
        if (session.RequestLoad(ExampleLevel()).Error != LevelError::None)
        {
            return 1;
        }
        for (usize step = 0; step < 40 && session.GetLoadStage() != LoadStage::Ready; ++step)
        {
            if (session.PollLoad() != Status::Success)
            {
                return 1;
            }
        }
        if (!session.Activate())
        {
            return 1;
        }
        for (usize tick = 0; tick < 120; ++tick)
        {
            if (session.World().RunTick({ .Axis = {0.5F, 0} }) != Status::Success)
            {
                return 1;
            }
        }
        LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                       "Headless reference: ticks={} hash={}",
                       session.World().GetTick(),
                       session.World().GetReplayHash());
        ludus::foundation::logging::LogSystem::Shutdown();
        return 0;
    }
#if LUDUS_TARGET_OS == LUDUS_OS_WEB
    (void)gApplication.Start();
    emscripten_set_main_loop(Frame, 0, true);
#else
    uint64 frameLimit = 0;
    if (argc == 3 && std::string_view(argv[1]) == "--frames")
    {
        const std::string_view text(argv[2]);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), frameLimit);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || frameLimit == 0)
        {
            return 1;
        }
    }
    Application application;
    if (!application.Start())
    {
        return 1;
    }
    while (application.Frame() != AppState::Stopped && application.GetState() != AppState::Failed)
    {
        if (frameLimit != 0 && application.GetPresentedFrames() >= frameLimit)
        {
            break;
        }
    }
    LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                   "Reference presented {} frames",
                   application.GetPresentedFrames());
    const auto state = application.GetState();
    application.Shutdown();
    ludus::foundation::logging::LogSystem::Shutdown();
    return state == AppState::Failed ? 1 : 0;
#endif
}
