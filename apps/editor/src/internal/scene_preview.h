#pragma once

#include <ludus/foundation/base/pointer.hpp>

#include "internal/scene_document.h"
#include <ludus/graphics/rhi/render.h>
#include <ludus/platform/base/window.h>

#include <QObject>
#include <QString>
#include <QTimer>

namespace ludus::editor
{
// Main-thread trusted renderer. Platform owns its separate native window;
// RHI is shut down before releasing that window. No project code is loaded.
class ScenePreview final : public QObject
{
    Q_OBJECT
public:
    explicit ScenePreview(SceneDocument* document, QObject* parent = nullptr);
    ~ScenePreview() override;
    [[nodiscard]] bool Start();
    void Stop() noexcept;
    void Tick();
    void Select(const world_demo::Name& id) noexcept;
    [[nodiscard]] world_demo::Name Selected() const noexcept
    {
        return Selected_;
    }
    [[nodiscard]] foundation::uint64 PresentedFrames() const noexcept
    {
        return Frames_;
    }
    [[nodiscard]] QString Status() const
    {
        return Status_;
    }
    [[nodiscard]] platform::NativeWindowInfo WindowInfo() const noexcept;
    [[nodiscard]] SceneStamp PresentedStamp() const noexcept
    {
        return Presented_;
    }
Q_SIGNALS:
    void Changed();

private:
    static void Record(void* context, const input::KeyboardRecord& record) noexcept;
    static void Reset(void* context, input::ResetReason reason, const input::FocusBaseline& baseline) noexcept;
    void Input();
    [[nodiscard]] bool Prepare() noexcept;
    [[nodiscard]] bool Render() noexcept;
    SceneDocument* Document_;
    foundation::UniquePtr<platform::Window> Window_;
    graphics::rhi::ShaderHandle Vertex_{};
    graphics::rhi::ShaderHandle Fragment_{};
    graphics::rhi::UniformHandle Uniform_{};
    graphics::rhi::PipelineHandle Pipeline_{};
    input::KeyboardRecord Queue_[64]{};
    foundation::usize QueueCount_ = 0;
    bool Suppressed_[input::KEY_COUNT]{};
    bool Arrows_[4]{};
    bool Cancel_ = false;
    bool OwnsSession_ = false;
    bool Created_ = false;
    bool Ready_ = false;
    foundation::uint32 Width_ = 0;
    foundation::uint32 Height_ = 0;
    foundation::uint64 Surface_ = 0;
    foundation::uint64 Frames_ = 0;
    SceneStamp Presented_{};
    world_demo::Vec2 Cursor_{};
    world_demo::Name Selected_{};
    QTimer Timer_;
    QString Status_;
};
} // namespace ludus::editor
