#pragma once
#include "world.h"
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/graphics/rhi/render.h>
#include <ludus/graphics/rhi/rhi.h>
#include <ludus/input/keyboard.h>
#include <ludus/platform/base/window.h>
#include <ludus/platform/browser/window.h>
namespace ludus::world_demo
{
enum class AppState : uint8
{
    Stopped,
    Loading,
    Playing,
    Failed
};
class Application final
{
public:
    [[nodiscard]] bool
    Start(graphics::rhi::BackendSelection selection = graphics::rhi::BackendSelection::Auto) noexcept;
    [[nodiscard]] AppState Frame() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] const Session& GetSession() const noexcept
    {
        return mSession;
    }
    [[nodiscard]] uint64 GetPresentedFrames() const noexcept
    {
        return mPresentedFrames;
    }
    [[nodiscard]] AppState GetState() const noexcept
    {
        return mState;
    }

private:
    [[nodiscard]] bool PrepareRenderer() noexcept;
    [[nodiscard]] bool Render(const platform::browser::WindowState& window) noexcept;
    void BrowserInput(const platform::browser::WindowState& window) noexcept;
    input::InputSystem mInput;
    foundation::UniquePtr<platform::Window> mWindow;
    Session mSession;
    RenderFrame mFrame;
    graphics::rhi::ShaderHandle mVertex;
    graphics::rhi::ShaderHandle mFragment;
    graphics::rhi::UniformHandle mUniform;
    graphics::rhi::PipelineHandle mPipeline;
    uint64 mPresentedFrames = 0;
    uint64 mLastTime = 0;
    uint64 mInputFrame = 0;
    bool mCreated = false;
    bool mRendererReady = false;
    bool mHadFocus = false;
    AppState mState = AppState::Stopped;
};
} // namespace ludus::world_demo
