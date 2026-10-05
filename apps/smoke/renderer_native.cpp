#include "internal/renderer.h"

#include "internal/text_demo.h"

namespace ludus::smoke::renderer
{
graphics::rhi::DeviceRequirements Requirements() noexcept
{
    return { .MinFrameDimension2D = 1 };
}
State Prepare() noexcept
{
    // Example: exercise the Ludus::Text CPU font API (shape + measure +
    // rasterize) once when the renderer becomes ready. The native renderer does
    // not yet draw text to the framebuffer (there is no GPU text/atlas module in
    // the tree), so the example reports its results through the logger. Run it
    // once so repeated Prepare() calls stay cheap.
    (void)text_demo::RunOnce();
    return State::Ready;
}
void Shutdown() noexcept {}
graphics::rhi::FrameStatus Render(const platform::browser::WindowState&, const Simulation&) noexcept
{
    const auto begun = graphics::rhi::BeginFrameStatus();
    return begun == graphics::rhi::FrameStatus::Ready ? graphics::rhi::EndFrameStatus() : begun;
}
} // namespace ludus::smoke::renderer
