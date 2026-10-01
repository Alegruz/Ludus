#include "internal/application.h"

#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <emscripten.h>

namespace
{
ludus::smoke::Application gApplication;
// Player-facing text stays concise; data attributes support deterministic QA.
// clang-format off
EM_JS(void, Present, (ludus::foundation::uint32 state, ludus::foundation::uint32 error,
                     ludus::foundation::uint32 frames, ludus::foundation::float64 x, ludus::foundation::float64 y), {
    const status = document.getElementById('status');
    const names = ['stopped', 'loading', 'playing', 'failed', 'device lost'];
    let message = ['Stopped. Select Restart to play.', 'Loading graphics…',
        'Ready. Click the canvas to play.', 'Graphics could not start. Try restarting.',
        'Graphics connection lost. Select Restart to try again.'][state];
    if (state == 3 && error == 3) message = 'WebGPU is unavailable. Use a browser and graphics driver that support WebGPU, then restart.';
    if (status.textContent != message) status.textContent = message;
    const instructions = document.getElementById('instructions');
    if (instructions) instructions.hidden = state != 2;
    status.dataset.state = names[state];
    status.dataset.frames = frames;
    status.dataset.x = x;
    status.dataset.y = y;
});
// clang-format on
void Frame() noexcept
{
    const auto state = gApplication.Tick();
    const auto& simulation = gApplication.GetSimulation();
    Present(static_cast<ludus::foundation::uint32>(state),
            static_cast<ludus::foundation::uint32>(gApplication.GetError()),
            gApplication.GetFrames(),
            simulation.X,
            simulation.Y);
}
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void Stop() noexcept
{
    gApplication.Shutdown();
}
extern "C" EMSCRIPTEN_KEEPALIVE void Restart() noexcept
{
    (void)gApplication.Start();
}
int main()
{
    ludus::foundation::logging::LogConfig config;
    config.EnableConsole = true;
    config.EnableFile = false;
    ludus::foundation::logging::LogSystem::Initialize(config);
    ludus::foundation::logging::SetCurrentThreadName("Main");
    Restart();
    emscripten_set_main_loop(Frame, 0, true);
}
