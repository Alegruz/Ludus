#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/viewport.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/native_interop.h>
#include <ludus/graphics/rhi/raster.h>
#include <ludus/platform/native_window.h>

#include "internal/render_scene.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

namespace ludus::editor
{
// Editor-only visualization; does not modify authored materials or scene lights.
enum class PreviewShading : foundation::uint8
{
    Solid,
    Albedo,
    Lit,
};
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
    [[nodiscard]] bool AdmitDevice(const graphics::rhi::NativeDeviceDescription& native);
    void Stop() noexcept;
    void Navigate(foundation::int32 camera,
                  foundation::float32 yaw,
                  foundation::float32 pitch,
                  foundation::float32 zoom,
                  bool reference);
    void FrameAll() noexcept;
    void SetShading(PreviewShading shading);
    [[nodiscard]] PreviewShading GetShading() const noexcept
    {
        return mShading;
    }
    void Resize(foundation::int32 width, foundation::int32 height);
    void Orbit(foundation::int32 dx, foundation::int32 dy);
    void Pan(foundation::int32 dx, foundation::int32 dy);
    void Zoom(foundation::int32 steps);
    [[nodiscard]] QJsonObject Navigation() const;
    void RestoreNavigation(const QJsonObject& state);
    void Tick();
    [[nodiscard]] bool Running() const noexcept
    {
        return OwnsDevice_ || mIsStarting;
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
    [[nodiscard]] bool FrameAvailable() const noexcept
    {
        return mHasFrame;
    }
    [[nodiscard]] platform::NativeWindowInfo WindowInfo() const noexcept;
    [[nodiscard]] bool TakeFrame(graphics::renderer::ViewportFrame& outFrame,
                                 graphics::renderer::ViewportImage& outImage) noexcept;
    void ReleaseFrame(graphics::renderer::ViewportFrame& inoutFrame) noexcept;
    [[nodiscard]] graphics::rhi::RasterStatus PinDevice(graphics::rhi::NativeDeviceLease& outLease) noexcept;
    [[nodiscard]] bool HasPendingFrame() const noexcept
    {
        return mHasPendingFrame;
    }
Q_SIGNALS:
    void Changed();
    void NavigationChanged();
    void FrameReady();
    void DeviceRequested();
    void ResettingFrames();
    void Stopped();
    void Failed(const QString& error);

private:
    [[nodiscard]] bool usesLitMaterials() const noexcept
    {
        return Scene_.Lit == true && mShading != PreviewShading::Albedo;
    }
    PreviewShading mShading = PreviewShading::Solid;
    struct Residency;
    foundation::UniquePtr<Residency> Residency_;
    bool LightingInitialized_ = false;
    graphics::renderer::RasterViewport mViewport;
    graphics::renderer::ViewportFrame mPendingFrame;
    graphics::renderer::ViewportImage mPendingImage;
    graphics::rhi::SubmissionToken mProducer;
    bool mHasPendingFrame = false;
    bool mHasFrame = false;
    bool mHasViewport = false;
    bool mIsAwaitingProducer = false;
    [[nodiscard]] bool prepare();
    void restartResources() noexcept;
    RenderScene Scene_;
    foundation::uint32 RequestedWidth_ = 640, RequestedHeight_ = 480;
    bool RestoredOrbit_ = false;
    QString CameraId_;
    void resetFrames() noexcept;
    void fail(const QString& message);

    graphics::rhi::DeviceHandle Device_{};
    graphics::rhi::SurfaceHandle Surface_{};
    graphics::renderer::Renderer Renderer_;
    graphics::renderer::Mesh Meshes_[6]{};
    graphics::rhi::TextureHandle Textures_[8]{};
    graphics::renderer::MaterialVersion Materials_[8]{};
    graphics::renderer::MaterialProgram Program_{};
    graphics::renderer::Snapshot Snapshot_{};
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
    bool mIsStarting = false;
    bool AssetsCreated_ = false;
    QTimer Timer_;
    QString Status_;
};
} // namespace ludus::editor
