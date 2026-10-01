#include "internal/renderer.h"
namespace ludus::smoke::renderer
{
State Prepare() noexcept
{
    return State::Ready;
}
void Shutdown() noexcept {}
graphics::rhi::FrameStatus Render(const platform::browser::WindowState&, const Simulation&) noexcept
{
    const auto begun = graphics::rhi::BeginFrameStatus();
    return begun == graphics::rhi::FrameStatus::Ready ? graphics::rhi::EndFrameStatus() : begun;
}
} // namespace ludus::smoke::renderer
