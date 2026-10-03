#include "internal/application.h"

#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <ludus/graphics/rhi/rhi.h>

#include <emscripten.h>

namespace
{
ludus::smoke::Application gApplication;
// Player-facing text stays concise; data attributes support deterministic QA.
// The selected backend and bounded per-attempt errors are QA metadata only and
// never appear in ordinary player text. An expected Auto fallback to WebGL 2 is
// not a failure, so it reads as "Ready", not an error.
// clang-format off
EM_JS(void, Present, (ludus::foundation::uint32 state, ludus::foundation::uint32 error,
                     ludus::foundation::uint32 frames, ludus::foundation::float64 x, ludus::foundation::float64 y,
                     ludus::foundation::uint32 backend, ludus::foundation::uint32 webgpuError,
                     ludus::foundation::uint32 webglError), {
    const status = document.getElementById('status');
    const names = ['stopped', 'loading', 'playing', 'failed', 'device lost'];
    const backends = ['vulkan', 'webgpu', 'webgl2'];
    let message = ['Stopped. Select Restart to play.', 'Loading graphics…',
        'Ready. Click the canvas to play.', 'Graphics could not start. Try restarting.',
        'Graphics connection lost. Select Restart to try again.'][state];
    // Both browser paths were attempted and failed: a concise, accessible, final
    // message that does not blame one API.
    if (state == 3 && webgpuError != 0 && webglError != 0) {
        message = 'This browser could not start WebGPU or WebGL 2. Update your browser and graphics drivers, then restart.';
    }
    if (status.textContent != message) status.textContent = message;
    const instructions = document.getElementById('instructions');
    if (instructions) instructions.hidden = state != 2;
    status.dataset.state = names[state];
    status.dataset.frames = frames;
    status.dataset.x = x;
    status.dataset.y = y;
    // QA metadata (not player-facing).
    status.dataset.backend = state == 2 ? backends[backend] : 'none';
    status.dataset.webgpuError = webgpuError;
    status.dataset.webglError = webglError;
});
// clang-format on
void Frame() noexcept
{
    const auto state = gApplication.Tick();
    const auto& simulation = gApplication.GetSimulation();
    // Selected backend is read live (meaningful while playing); the per-attempt
    // errors come from the application snapshot so they survive a failed session
    // whose RHI startup info was already cleared on teardown.
    const auto startup = ludus::graphics::rhi::GetStartup();
    Present(static_cast<ludus::foundation::uint32>(state),
            static_cast<ludus::foundation::uint32>(gApplication.GetError()),
            gApplication.GetFrames(),
            simulation.X,
            simulation.Y,
            static_cast<ludus::foundation::uint32>(startup.SelectedBackend),
            static_cast<ludus::foundation::uint32>(gApplication.GetWebGpuError()),
            static_cast<ludus::foundation::uint32>(gApplication.GetWebGl2Error()));
}
} // namespace
namespace
{
// Reads an optional backend selection for diagnosis: ?backend=auto|webgpu|webgl2
// (default Auto). Player builds use Auto; QA uses the explicit values. The value
// comes from the page URL, not from engine code, and maps to BackendSelection.
// clang-format off
EM_JS(ludus::foundation::uint32, SelectedPolicy, (), {
    try {
        const value = new URLSearchParams(location.search).get('backend');
        return value === 'webgpu' ? 1 : value === 'webgl2' ? 2 : 0;
    } catch (_) { return 0; }
});
// clang-format on
ludus::graphics::rhi::BackendSelection Policy() noexcept
{
    switch (SelectedPolicy())
    {
        case 1:
            return ludus::graphics::rhi::BackendSelection::WebGPU;
        case 2:
            return ludus::graphics::rhi::BackendSelection::WebGL2;
        default:
            return ludus::graphics::rhi::BackendSelection::Auto;
    }
}
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void Stop() noexcept
{
    gApplication.Shutdown();
}
extern "C" EMSCRIPTEN_KEEPALIVE void Restart() noexcept
{
    (void)gApplication.Start(Policy());
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
