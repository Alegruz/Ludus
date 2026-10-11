#include "internal/render_preview.h"

#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>
#include <ludus/graphics/rhi/rhi.h>

#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
#    include "renderer_composite.h"
#    include "renderer_flat.h"
#    include "renderer_material.h"
#endif

namespace ludus::editor
{
namespace
{
using namespace foundation;
namespace rhi = graphics::rhi;
namespace rr = graphics::renderer;
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
bool Accepted(rhi::RasterStatus s) noexcept
{
    return s == rhi::RasterStatus::Ready || s == rhi::RasterStatus::Pending;
}
#endif
} // namespace
RenderPreview::RenderPreview(QObject* parent) : QObject(parent)
{
    Timer_.setInterval(16);
    connect(&Timer_, &QTimer::timeout, this, &RenderPreview::Tick);
}
RenderPreview::~RenderPreview()
{
    Stop();
}
platform::NativeWindowInfo RenderPreview::WindowInfo() const noexcept
{
    return Window_ ? Window_->GetNativeWindowInfo() : platform::NativeWindowInfo{};
}
bool RenderPreview::SetScene(const RenderScene& scene)
{
    bool valid = scene.Identity.isEmpty()
                     ? (scene.Meshes.isEmpty() && scene.Textures.isEmpty() && scene.Surfaces.isEmpty() &&
                        scene.Objects.isEmpty() && scene.Cameras.isEmpty())
                     : (!scene.Meshes.isEmpty() && scene.Meshes.size() <= 6 && scene.Textures.size() <= 8 &&
                        scene.Surfaces.size() <= 8 && scene.Objects.size() <= 128 && scene.Cameras.size() <= 128);
    for (const auto& surface : scene.Surfaces)
    {
        valid = valid && surface.Texture >= 0 && surface.Texture < scene.Textures.size();
    }
    for (const auto& object : scene.Objects)
    {
        valid =
            valid && object.Mesh >= 0 && object.Mesh < scene.Meshes.size() && object.Texture >= -1 &&
            object.Texture < scene.Textures.size() &&
            (object.Texture < 0 ? object.Surface == -1 : object.Surface >= 0 && object.Surface < scene.Surfaces.size());
    }
    if (!valid)
    {
        Status_ = QStringLiteral(
            "Preview snapshot exceeds its validated asset/object/material bounds. Previous data retained.");
        Q_EMIT Changed();
        return false;
    }
    if (scene.Identity.isEmpty())
    {
        Stop();
        Scene_ = {};
        OrbitCenter_ = {};
        OrbitRadius_ = 1;
        Status_.clear();
        return true;
    }
    if (Scene_.Identity == scene.Identity)
    {
        return true;
    }
    bool sameResources = Scene_.AssetsIdentity == scene.AssetsIdentity && Scene_.Objects.size() == scene.Objects.size();
    bool sameGeometry = sameResources;
    if (sameResources)
    {
        for (foundation::int32 i = 0; i < scene.Objects.size(); ++i)
        {
            const auto& old = Scene_.Objects[i];
            const auto& next = scene.Objects[i];
            sameResources = sameResources && old.Id == next.Id && old.Mesh == next.Mesh &&
                            old.Texture == next.Texture && old.Tint == next.Tint;
            sameGeometry = sameGeometry && old.Transform == next.Transform;
        }
    }
    if (Scene_.Identity.isEmpty())
    {
        OrbitCenter_ = scene.Center;
        OrbitRadius_ = scene.Radius;
    }
    Scene_ = scene;
    Status_ = QStringLiteral("Applied World preview data updated; preparing the renderer.");
    Camera_ = -1;
    Frames_ = 0;
    if (OwnsDevice_)
    {
        if (sameResources && Phase_ >= 5)
        {
            if (!sameGeometry)
            {
                if (Phase_ == 6)
                {
                    (void)Renderer_.Release(View_);
                }
                (void)Renderer_.Release(Snapshot_);
                Phase_ = 4;
            }
            ViewDirty_ = true;
        }
        else
        {
            RestartResources();
        }
    }
    Q_EMIT Changed();
    return true;
}
void RenderPreview::RestartResources() noexcept
{
    Renderer_.Reset();
    for (auto& texture : Textures_)
    {
        (void)rhi::Destroy(Device_, texture);
        texture = {};
    }
    for (auto& mesh : Meshes_)
    {
        mesh = {};
    }
    for (auto& material : Materials_)
    {
        material = {};
    }
    Program_ = {};
    Snapshot_ = {};
    View_ = {};
    Report_ = {};
    Phase_ = 0;
    AssetsCreated_ = false;
    ViewDirty_ = true;
}
bool RenderPreview::Start()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (Scene_.Identity.isEmpty())
    {
        Status_ = QStringLiteral("Load and validate a World preview first.");
        Q_EMIT Changed();
        return false;
    }
    if (Running())
    {
        return true;
    }
    if (rhi::GetStartup().State != rhi::StartupState::Idle)
    {
        Status_ = QStringLiteral("Another authoring surface owns rendering. Close it before opening the 3D preview.");
        Q_EMIT Changed();
        return false;
    }
    platform::WindowManager manager;
    if (!manager.Initialize({}) ||
        !manager.CreateWindow(
            { .Name = "Ludus World 3D preview - navigation is in the World panel", .Width = 960, .Height = 640 },
            Window_))
    {
        Status_ = QStringLiteral("Native preview window is unavailable. World editing remains available.");
        Q_EMIT Changed();
        return false;
    }
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const auto started = rhi::CreateDevice({ .Name = "Ludus World preview", .Version = 1 },
                                           Window_->GetNativeWindowInfo(),
                                           description,
                                           Device_,
                                           Surface_);
    if (started != rhi::DeviceStatus::Ready && started != rhi::DeviceStatus::Pending)
    {
        Window_.Reset();
        Status_ =
            QStringLiteral("Renderer admission failed. No game code was loaded; World editing remains available.");
        Q_EMIT Changed();
        return false;
    }
    OwnsDevice_ = true;
    Width_ = Height_ = 0;
    Frames_ = 0;
    RestartResources();
    Status_ = QStringLiteral("Preparing 3D preview…");
    Timer_.start();
    Q_EMIT Changed();
    return true;
#else
    Status_ = QStringLiteral("3D preview requires the native editor built with LUDUS_EDITOR_RENDER_PREVIEW and pinned "
                             "shader tools. World data remains editable.");
    Q_EMIT Changed();
    return false;
#endif
}
void RenderPreview::Stop() noexcept
{
    const bool wasRunning = static_cast<bool>(Window_);
    Timer_.stop();
    if (OwnsDevice_)
    {
        RestartResources();
        (void)rhi::DestroyDevice(Device_);
    }
    OwnsDevice_ = false;
    Window_.Reset();
    Device_ = {};
    Surface_ = {};
    Width_ = Height_ = 0;
    Frames_ = 0;
    if (wasRunning)
    {
        Q_EMIT Stopped();
    }
}
void RenderPreview::FrameAll() noexcept
{
    OrbitCenter_ = Scene_.Center;
    OrbitRadius_ = Scene_.Radius;
    ViewDirty_ = true;
}
void RenderPreview::Navigate(int32 camera, float32 yaw, float32 pitch, float32 zoom, bool reference)
{
    if (Camera_ != camera || Yaw_ != yaw || Pitch_ != pitch || Zoom_ != zoom || Reference_ != reference)
    {
        Camera_ = camera;
        Yaw_ = yaw;
        Pitch_ = pitch;
        Zoom_ = zoom;
        Reference_ = reference;
        ViewDirty_ = true;
    }
}
bool RenderPreview::Prepare()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    auto advance = [this](rhi::RasterStatus status) {
        if (!Accepted(status))
        {
            return false;
        }
        ++Phase_;
        return true;
    };
    if (Phase_ == 0)
    {
        return advance(Renderer_.Initialize(Device_,
                                            ludus::shaders::renderer_flat::Vertex(),
                                            ludus::shaders::renderer_flat::Fragment()));
    }
    if (Phase_ == 1)
    {
        const auto s = Renderer_.Poll();
        return s == rhi::RasterStatus::Pending ||
               (s == rhi::RasterStatus::Ready &&
                advance(Renderer_.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                                  ludus::shaders::renderer_composite::Fragment())));
    }
    if (Phase_ == 2)
    {
        const auto s = Renderer_.PollViews();
        return s == rhi::RasterStatus::Pending ||
               (s == rhi::RasterStatus::Ready &&
                advance(Renderer_.PrewarmMaterialProgram(ludus::shaders::renderer_material::Vertex(),
                                                         ludus::shaders::renderer_material::Fragment(),
                                                         Program_)));
    }
    if (Phase_ == 3)
    {
        const auto programStatus = Renderer_.GetStatus(Program_);
        if (programStatus == rhi::RasterStatus::Pending)
        {
            return true;
        }
        if (programStatus != rhi::RasterStatus::Ready)
        {
            return false;
        }
        if (!AssetsCreated_)
        {
            for (int32 i = 0; i < Scene_.Meshes.size(); ++i)
            {
                const auto& mesh = Scene_.Meshes[i];
                if (!Accepted(Renderer_.CreateMesh(
                        {
                            .Positions = mesh.Positions.constData(),
                            .VertexCount = static_cast<usize>(mesh.Positions.size()),
                            .Indices = mesh.Indices.constData(),
                            .IndexCount = static_cast<usize>(mesh.Indices.size()),
                            .Texcoords = mesh.Uvs.constData(),
                        },
                        Meshes_[i])))
                {
                    return false;
                }
            }
            for (int32 i = 0; i < Scene_.Textures.size(); ++i)
            {
                const auto& image = Scene_.Textures[i].Pixels;
                if (!Accepted(
                        rhi::CreateTexture(Device_,
                                           {
                                               .Width = static_cast<uint32>(image.width()),
                                               .Height = static_cast<uint32>(image.height()),
                                               .Format = rhi::RasterFormat::Rgba8Srgb,
                                           },
                                           {
                                               .Bytes = {image.constBits(), static_cast<usize>(image.sizeInBytes())},
                                               .RowPitch = static_cast<usize>(image.bytesPerLine()),
                                           },
                                           Textures_[i])))
                {
                    return false;
                }
            }
            AssetsCreated_ = true;
        }
        for (int32 i = 0; i < Scene_.Meshes.size(); ++i)
        {
            const auto s = Renderer_.GetStatus(Meshes_[i]);
            if (!Accepted(s))
            {
                return false;
            }
            if (s == rhi::RasterStatus::Pending)
            {
                return true;
            }
        }
        for (int32 i = 0; i < Scene_.Textures.size(); ++i)
        {
            const auto s = rhi::GetStatus(Device_, Textures_[i]);
            if (!Accepted(s))
            {
                return false;
            }
            if (s == rhi::RasterStatus::Pending)
            {
                return true;
            }
        }
        for (int32 i = 0; i < Scene_.Surfaces.size(); ++i)
        {
            const auto& surface = Scene_.Surfaces[i];
            rr::MaterialDescription description;
            description.Texture = Textures_[surface.Texture];
            description.Tint = surface.Tint;
            if (!Accepted(Renderer_.CreateMaterial(Program_, description, Materials_[i])))
            {
                return false;
            }
        }
        ++Phase_;
        return true;
    }
    if (Phase_ == 4)
    {
        rr::SceneItem items[128]{};
        for (int32 i = 0; i < Scene_.Objects.size(); ++i)
        {
            const auto& object = Scene_.Objects[i];
            auto& item = items[i];
            item.Geometry = Meshes_[object.Mesh];
            item.Transform = object.Transform;
            item.Material.Color = object.Tint;
            item.SourceId = static_cast<uint64>(i) + 1;
            if (object.Texture >= 0)
            {
                const auto s = Renderer_.GetStatus(Materials_[object.Surface]);
                if (!Accepted(s))
                {
                    return false;
                }
                if (s == rhi::RasterStatus::Pending)
                {
                    return true;
                }
                item.Surface = Materials_[object.Surface];
            }
        }
        if (!Accepted(Renderer_.CreateSnapshot(items, static_cast<usize>(Scene_.Objects.size()), Snapshot_)))
        {
            return false;
        }
        ++Phase_;
        return true;
    }
    if (Width_ == 0 || Height_ == 0)
    {
        return true;
    }
    if (ViewDirty_)
    {
        rr::ViewDescription description;
        PreviewViewOptions options;
        options.Camera = Camera_;
        options.OrbitDegrees = {Yaw_, Pitch_};
        options.Distance = OrbitRadius_ * Zoom_;
        options.Width = Width_;
        options.Height = Height_;
        options.Reference = Reference_;
        options.OrbitCenter = &OrbitCenter_;
        options.OrbitRadius = OrbitRadius_;
        if (!Scene_.View(options, description, Status_))
        {
            return false;
        }
        Description_ = description;
        if (Phase_ == 6)
        {
            (void)Renderer_.Release(View_);
        }
        if (!Accepted(Renderer_.PrepareView(Snapshot_, description, View_, Report_)))
        {
            return false;
        }
        ViewDirty_ = false;
        FreshReport_ = true;
        Phase_ = 6;
    }
    return Accepted(Renderer_.GetStatus(View_));
#else
    return false;
#endif
}
void RenderPreview::Tick()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (!Window_)
    {
        return;
    }
    if (!Window_->HandleEvent({}))
    {
        Stop();
        Status_ = QStringLiteral("3D preview closed. Reopen it to continue; saved World data is unchanged.");
        Q_EMIT Changed();
        return;
    }
    rhi::DeviceInfo device;
    const auto ready = rhi::GetDeviceInfo(Device_, device);
    if (ready == rhi::DeviceStatus::Pending)
    {
        return;
    }
    bool good =
        ready == rhi::DeviceStatus::Ready && rhi::PollLifetime(Device_) == rhi::RasterStatus::Ready && Prepare();
    if (good && Phase_ >= 5)
    {
        const auto window = Window_->GetNativeWindowInfo();
        const auto target = rhi::SetFrameTarget(Device_, Surface_, { .Width = window.Width, .Height = window.Height });
        if (target == rhi::DeviceStatus::Skipped)
        {
            return;
        }
        const auto acquired = target == rhi::DeviceStatus::Ready ? rhi::BeginFrame(Device_, Surface_) : target;
        if (acquired == rhi::DeviceStatus::Skipped)
        {
            return;
        }
        good = acquired == rhi::DeviceStatus::Ready;
        if (good)
        {
            rhi::FrameInfo frame;
            good = rhi::GetFrameInfo(Device_, Surface_, frame) == rhi::DeviceStatus::Ready;
            bool drew = false;
            if (good && frame.Width == Width_ && frame.Height == Height_ && Phase_ == 6)
            {
                const auto status = Renderer_.GetStatus(View_);
                if (status == rhi::RasterStatus::Ready)
                {
                    good = Renderer_.DrawView(View_, { .Clear = {0.035F, 0.045F, 0.06F, 1}, .ClearDepth = 0 }) ==
                           rhi::RasterStatus::Ready;
                    drew = good;
                }
                else
                {
                    good = status == rhi::RasterStatus::Pending;
                }
            }
            else if (good)
            {
                Width_ = frame.Width;
                Height_ = frame.Height;
                ViewDirty_ = true;
            }
            const auto ended = rhi::EndFrame(Device_);
            good = good && ended == rhi::DeviceStatus::Ready;
            if (good && drew)
            {
                ++Frames_;
                if (FreshReport_ || Frames_ == 1 || Frames_ % 60 == 0)
                {
                    FreshReport_ = false;
                    Status_ = QStringLiteral(
                                  "3D rendered · %1×%2 · submitted %3 · visible %4 · culled %5 · draws %6 · frame %7%8")
                                  .arg(Width_)
                                  .arg(Height_)
                                  .arg(Report_.Submitted)
                                  .arg(Report_.Visible)
                                  .arg(Report_.Culled)
                                  .arg(Report_.Draws)
                                  .arg(Frames_)
                                  .arg(Reference_ ? QStringLiteral(" · reference order/culling disabled") : QString());
                    Q_EMIT Changed();
                }
            }
        }
    }
    if (!good)
    {
        const auto detail = Status_;
        Stop();
        Status_ = QStringLiteral(
                      "3D renderer stopped or reached its capacity. Reopen to retry. World edits are preserved. ") +
                  detail;
        Q_EMIT Changed();
    }
#endif
}
} // namespace ludus::editor
