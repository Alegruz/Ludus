#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/raster.h>
#include <ludus/platform/base/window.h>

#include "internal/render_scene.h"

#include <QObject>
#include <QTimer>

namespace ludus::editor
{
// Main-thread renderer/device owner. Never destroys another preview's session.
class RenderPreview final : public QObject
{
    Q_OBJECT
public:
    explicit RenderPreview(QObject* parent = nullptr);
    ~RenderPreview() override;
    // Reject malformed private snapshots before touching fixed GPU handle arrays.
    [[nodiscard]] bool SetScene(const RenderScene& scene);
    [[nodiscard]] bool Start();
    void Stop() noexcept;
    void Navigate(foundation::int32 camera,
                  foundation::float32 yaw,
                  foundation::float32 pitch,
                  foundation::float32 zoom,
                  bool reference);
    void FrameAll() noexcept;
    void Tick();
    [[nodiscard]] bool Running() const noexcept
    {
        return static_cast<bool>(Window_);
    }
    [[nodiscard]] foundation::uint64 Frames() const noexcept
    {
        return Frames_;
    }
    [[nodiscard]] QString Status() const
    {
        return Status_;
    }
    [[nodiscard]] const RenderScene& Scene() const noexcept
    {
        return Scene_;
    }
    [[nodiscard]] graphics::renderer::ViewDescription ViewDescription() const noexcept
    {
        return Description_;
    }
    [[nodiscard]] graphics::renderer::ViewReport Report() const noexcept
    {
        return Report_;
    }
    [[nodiscard]] platform::NativeWindowInfo WindowInfo() const noexcept;
Q_SIGNALS:
    void Changed();
    void Stopped();

private:
    [[nodiscard]] bool Prepare();
    void RestartResources() noexcept;
    RenderScene Scene_;
    foundation::UniquePtr<platform::Window> Window_;
    graphics::rhi::DeviceHandle Device_{};
    graphics::rhi::SurfaceHandle Surface_{};
    graphics::renderer::Renderer Renderer_;
    graphics::renderer::Mesh Meshes_[6]{};
    graphics::rhi::TextureHandle Textures_[8]{};
    graphics::renderer::MaterialVersion Materials_[8]{};
    graphics::renderer::MaterialProgram Program_{};
    graphics::renderer::Snapshot Snapshot_{};
    graphics::renderer::PreparedView View_{};
    graphics::renderer::ViewReport Report_{};
    graphics::renderer::ViewDescription Description_{};
    foundation::math::Vector3 OrbitCenter_{};
    foundation::float32 OrbitRadius_ = 1;
    foundation::uint32 Phase_ = 0;
    foundation::uint32 Width_ = 0, Height_ = 0;
    foundation::uint64 Frames_ = 0;
    foundation::int32 Camera_ = -1;
    foundation::float32 Yaw_ = 25, Pitch_ = 20, Zoom_ = 3;
    bool Reference_ = false;
    bool ViewDirty_ = true;
    bool FreshReport_ = true;
    bool OwnsDevice_ = false;
    bool AssetsCreated_ = false;
    QTimer Timer_;
    QString Status_;
};
} // namespace ludus::editor
