#include "internal/frame_presenter.h"

#include <ludus/foundation/base/types.h>
#include <ludus/graphics/rhi/rhi.h>
#include <ludus/input/actions.h>
#include <ludus/input/key.h>
#include <ludus/input/keyboard_event.h>
#include <ludus/platform/keyboard_sink.h>

#include <cstring>

namespace ludus::runtime::game_host
{
namespace
{
using ludus::foundation::float32;
using ludus::foundation::float64;
using ludus::foundation::uint32;
[[nodiscard]] bool Finite(float32 value) noexcept
{
    uint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000U) != 0x7f800000U;
}
[[nodiscard]] float64 ClampColor(float32 value) noexcept
{
    return value < 0 ? 0 : (value > 1 ? 1 : static_cast<float64>(value));
}
} // namespace
FramePresenter::~FramePresenter() noexcept
{
    if (Window_)
    {
        Window_->DetachKeyboardSink();
    }
    if (Rendering_)
    {
        graphics::rhi::Shutdown();
    }
    Window_.Reset();
}
RunResult FramePresenter::Start(Presentation mode) noexcept
{
    if (!Input_.IsValid())
    {
        return RunResult::Internal;
    }
    constexpr input::Binding bindings[] = {
        { .Action = 0, .PhysicalKey = input::Key::KeyA },
        { .Action = 0, .PhysicalKey = input::Key::ArrowLeft },
        { .Action = 1, .PhysicalKey = input::Key::KeyD },
        { .Action = 1, .PhysicalKey = input::Key::ArrowRight },
        { .Action = 2, .PhysicalKey = input::Key::Space },
    };
    if (Input_.ReplaceBindings({bindings}) != input::BindingStatus::Ok)
    {
        return RunResult::Internal;
    }
    if (mode == Presentation::Headless)
    {
        return RunResult::Ok;
    }
    platform::WindowManager manager;
    if (!manager.Initialize({}) || !manager.CreateWindow(
                                       {
                                           .Name = "Ludus game",
                                           .Width = 800,
                                           .Height = 600,
                                       },
                                       Window_))
    {
        return RunResult::WindowUnavailable;
    }
    Window_->AttachKeyboardSink(
    {
        .OnRecord =
            [](void* user, const input::KeyboardRecord& record) noexcept {
                (void)static_cast<input::InputSystem*>(user)->Ingest(record);
            },
        .OnReset =
            [](void* user, input::ResetReason reason, const input::FocusBaseline& baseline) noexcept {
                static_cast<input::InputSystem*>(user)->RequestReset(reason, baseline);
            },
        .UserData = &Input_,
    });
    const auto started = graphics::rhi::Start(
        {
            .Name = "Ludus GameHost",
            .Version = 1,
        },
        Window_->GetNativeWindowInfo());
    Rendering_ = true; // Failed startup still owns an RHI session until Shutdown.
    return started == graphics::rhi::StartStatus::Ready ? RunResult::Ok : RunResult::RenderingUnavailable;
}
bool FramePresenter::Poll() noexcept
{
    if (Window_ && !Window_->HandleEvent({}))
    {
        return false;
    }
    return Input_.ConsumeStep(++InputStep_) == input::StepStatus::Ok;
}
void FramePresenter::ResetInput() noexcept
{
    input::FocusBaseline baseline;
    baseline.Focused = Input_.GetKeyboardSnapshot().Focused;
    for (ludus::foundation::usize i = 1; i < input::KEY_COUNT; ++i)
    {
        baseline.Keys[i] = Input_.GetLiveDown(static_cast<input::Key>(i));
    }
    Input_.RequestReset(input::ResetReason::ExplicitLive, baseline);
}
void FramePresenter::FillInput(game_api::FrameInput& frame) const noexcept
{
    for (input::ActionId i = 0; i < 3; ++i)
    {
        input::ActionState state;
        if (Input_.GetAction(i, state) == input::ActionStatus::Ok && state.Value != 0)
        {
            frame.HeldActions |= ludus::foundation::uint32{1} << i;
        }
    }
    if (Window_)
    {
        const auto window = Window_->GetNativeWindowInfo();
        frame.Width = window.Width;
        frame.Height = window.Height;
    }
}
RunResult FramePresenter::Present(const game_api::RenderParams& render) noexcept
{
    if (!Finite(render.ClearRed) || !Finite(render.ClearGreen) || !Finite(render.ClearBlue) ||
        !Finite(render.ClearAlpha) || !Finite(render.Uniform0) || !Finite(render.Uniform1) ||
        !Finite(render.Uniform2) || !Finite(render.Uniform3))
    {
        return RunResult::Internal;
    }
    if (!Rendering_)
    {
        return RunResult::Ok;
    }
    namespace rhi = graphics::rhi;
    const auto window = Window_->GetNativeWindowInfo();
    const auto target = rhi::SetFrameTarget(
    {
        .Width = window.Width,
        .Height = window.Height,
        .Red = ClampColor(render.ClearRed),
        .Green = ClampColor(render.ClearGreen),
        .Blue = ClampColor(render.ClearBlue),
        .Alpha = ClampColor(render.ClearAlpha),
    });
    if (target == rhi::FrameStatus::Skipped)
    {
        return RunResult::Ok;
    }
    if (target != rhi::FrameStatus::Ready)
    {
        return RunResult::RenderingUnavailable;
    }
    const auto begun = rhi::BeginFrameStatus();
    if (begun == rhi::FrameStatus::Skipped)
    {
        return RunResult::Ok;
    }
    if (begun != rhi::FrameStatus::Ready || rhi::EndFrameStatus() != rhi::FrameStatus::Ready)
    {
        return RunResult::RenderingUnavailable;
    }
    ++PresentedFrames_;
    return RunResult::Ok;
}
} // namespace ludus::runtime::game_host
