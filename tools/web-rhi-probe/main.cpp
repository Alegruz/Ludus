#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/graphics/rhi/rhi.h>
#include <ludus/platform/base/window.h>

#include <emscripten.h>

namespace
{
using namespace ludus::foundation;
using namespace ludus::graphics;
ludus::foundation::UniquePtr<ludus::platform::Window> gWindow;
// clang-format off
EM_JS(void, Status, (uint32 state, uint32 error, uint32 limit), {
    const names = ['idle', 'pending', 'ready', 'failed', 'device lost'];
    const errors = ['none', 'invalid canvas/window', 'instance unavailable', 'adapter unavailable', 'device unavailable', 'surface unavailable', 'rendering unavailable', 'validation error', 'device lost', 'generation exhausted'];
    document.getElementById('status').textContent = names[state] + ' | ' + errors[error] + ' | texture limit ' + limit + ' | frames reserved for W5';
    document.getElementById('status').dataset.state = names[state];
});
// clang-format on
void Frame() noexcept
{
    const auto state = rhi::GetStartup();
    Status(static_cast<uint32>(state.State), static_cast<uint32>(state.Error), state.MaxTextureDimension2D);
}
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void Stop() noexcept
{
    rhi::Shutdown();
}
extern "C" EMSCRIPTEN_KEEPALIVE void Restart() noexcept
{
    rhi::Shutdown();
    gWindow.Reset();
    ludus::platform::WindowManager manager;
    if (manager.Initialize({}) && manager.CreateWindow({}, gWindow))
    {
        (void)rhi::Start({ .Name = "W4 lifecycle" }, gWindow->GetNativeWindowInfo());
    }
}
int main()
{
    Restart();
    emscripten_set_main_loop(Frame, 60, true);
}
