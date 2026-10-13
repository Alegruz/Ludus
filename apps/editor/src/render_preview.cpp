#include "internal/render_preview.h"

#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/graph.h>
#include <ludus/graphics/rhi/lifetime.h>
#include <ludus/graphics/rhi/raster.h>
#include <ludus/graphics/rhi/rhi.h>
#include <new>

#include <QElapsedTimer>

#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
#    include "renderer_composite.h"
#    include "renderer_flat.h"
#    include "renderer_lit.h"
#    include "renderer_lit_shadow.h"
#    include "renderer_material.h"
#    include "renderer_output.h"
#    include "renderer_shadow.h"
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
struct RenderPreview::Residency final
{
    QSharedPointer<resources::SceneDefinition> Definition;
    resources::SceneDefinitionAdapter Adapter;
    resources::Manager Manager;
    resources::Lease Lease;
    ~Residency() noexcept
    {
        (void)Lease.Release();
        auto status = Manager.Shutdown();
        // CPU-only retirement is immediate. Cover the complete coordinator cursor,
        // including scope closure, before destroying the borrowed provider context.
        for (uint32 i = 0; i < 4 && status == resources::Status::Pending; ++i)
        {
            Manager.Service(64);
            status = Manager.Shutdown();
        }
        LUDUS_REQUIRE(status == resources::Status::Ok);
    }
    bool Initialize(const RenderScene& scene) noexcept
    {
        Definition = scene.Definition;
        content::Resource resource;
        const auto id = scene.LogicalId.toUtf8();
        const auto path = QByteArray("revisions/") + scene.Identity.toHex() + QByteArray(".luscn");
        if (!resource.Id.Set({id.constData(), static_cast<usize>(id.size())}) ||
            !resource.Path.Set({path.constData(), static_cast<usize>(path.size())}))
        {
            return false;
        }
        resource.Type = content::Kind::CookedScene;
        content::Catalog catalog;
        if (Adapter.Initialize(resource, *Definition) != resources::Status::Ok ||
            catalog.Put(resource) != content::Status::Ok)
        {
            return false;
        }
        const auto adapter = Adapter.Descriptor();
        resources::AssetHandle asset;
        resources::ScopeHandle scope;
        content::ResourceId name;
        resources::RequestHandle request;
        (void)name.Set("editor-level");
        if (Manager.Initialize(catalog, &adapter, 1) != resources::Status::Ok ||
            Manager.Resolve(resource.Id, resource.Type, asset) != resources::Status::Ok ||
            Manager.OpenScope(name, scope) != resources::Status::Ok ||
            Manager.BeginAcquire(scope, asset, request) != resources::Status::Ok)
        {
            return false;
        }
        Manager.Service();
        Manager.Service();
        Manager.Service();
        const void* payload = nullptr;
        return Manager.TakeLease(request, Lease) == resources::Status::Ok &&
               Manager.Borrow(Lease.Token(), content::Kind::CookedScene, payload) == resources::Status::Ok &&
               payload == Definition.data();
    }
};
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
    return { .Width = RequestedWidth_, .Height = RequestedHeight_ };
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
        valid = valid && surface.Texture >= (scene.Lit ? -1 : 0) && surface.Texture < scene.Textures.size();
    }
    for (const auto& object : scene.Objects)
    {
        valid = valid && object.Mesh >= 0 && object.Mesh < scene.Meshes.size() && object.Texture >= -1 &&
                object.Texture < scene.Textures.size() &&
                (scene.Lit || object.Texture >= 0 ? object.Surface >= 0 && object.Surface < scene.Surfaces.size()
                                                  : object.Surface == -1);
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
        Residency_.Reset();
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
    if (Scene_.Identity.isEmpty() && !RestoredOrbit_)
    {
        OrbitCenter_ = scene.Center;
        OrbitRadius_ = scene.Radius;
    }
    foundation::UniquePtr<Residency> residency;
    if (scene.Lit)
    {
        residency.Reset(new (std::nothrow) Residency);
        if (!residency || !scene.Definition || !residency->Initialize(scene))
        {
            Status_ = QStringLiteral("Resource-manager admission failed; previous scene retained.");
            Q_EMIT Changed();
            return false;
        }
    }
    // Retire the previous lease and provider through the local owner.
    Residency_.Swap(residency);
    Scene_ = scene;
    if (!CameraId_.isEmpty())
    {
        Camera_ = -1;
        for (int32 i = 0; i < Scene_.Cameras.size(); ++i)
        {
            if (Scene_.Cameras[i].Id == CameraId_)
            {
                Camera_ = i;
            }
        }
    }
    Status_ = QStringLiteral("World preview snapshot updated; preparing the renderer.");
    if (Camera_ >= Scene_.Cameras.size())
    {
        Camera_ = -1;
    }
    if (mHasPendingFrame == true)
    {
        static_cast<void>(mViewport.Release(mPendingFrame));
        mHasPendingFrame = false;
    }
    Frames_ = 0;
    if (OwnsDevice_)
    {
        if (sameResources && Phase_ >= 5)
        {
            if (!sameGeometry)
            {
                (void)Renderer_.Release(Snapshot_);
                Phase_ = 4;
            }
            ViewDirty_ = true;
        }
        else
        {
            restartResources();
        }
    }
    Q_EMIT Changed();
    return true;
}
void RenderPreview::restartResources() noexcept
{
    resetFrames();
    LightingInitialized_ = false;
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
    Timer_.setInterval(16);
    if (Running())
    {
        Timer_.start();
        return true;
    }
    if (rhi::GetStartup().State != rhi::StartupState::Idle)
    {
        Status_ = QStringLiteral("Another authoring surface owns rendering. Close it before opening the 3D preview.");
        Q_EMIT Changed();
        return false;
    }
    mIsStarting = true;
    Status_ = QStringLiteral("Preparing the shared Qt GPU device…");
    Q_EMIT DeviceRequested();
    Q_EMIT Changed();
    return mIsStarting == true || OwnsDevice_ == true;
#else
    Status_ = QStringLiteral("3D preview requires the native editor built with LUDUS_EDITOR_RENDER_PREVIEW and pinned "
                             "shader tools. World data remains editable.");
    Q_EMIT Changed();
    return false;
#endif
}
bool RenderPreview::AdmitDevice(const rhi::NativeDeviceDescription& native)
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (mIsStarting == false || OwnsDevice_ == true)
    {
        return false;
    }
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    const rhi::DeviceStatus started = rhi::CreateNativeDevice({ .Name = "Ludus shared editor viewport", .Version = 1 },
                                                              native,
                                                              description,
                                                              8,
                                                              8,
                                                              Device_,
                                                              Surface_);
    if (started != rhi::DeviceStatus::Ready)
    {
        Status_ =
            QStringLiteral("The Qt GPU context does not support native viewport sharing. Editing remains available.");
        Q_EMIT Changed();
        Q_EMIT Failed(Status_);
        return false;
    }
    mIsStarting = false;
    OwnsDevice_ = true;
    Width_ = Height_ = 0;
    Frames_ = 0;
    restartResources();
    Timer_.start();
    return true;
#else
    static_cast<void>(native);
    return false;
#endif
}
void RenderPreview::Stop() noexcept
{
    const bool wasRunning = Running();
    mIsStarting = false;
    Timer_.stop();
    if (OwnsDevice_)
    {
        resetFrames();
        restartResources();
        (void)rhi::DestroyDevice(Device_);
    }
    OwnsDevice_ = false;
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
    RestoredOrbit_ = false;
    Navigate(-1, 25, 20, 3, false);
    Q_EMIT NavigationChanged();
    ViewDirty_ = true;
    if (Running())
    {
        Timer_.start();
    }
}
void RenderPreview::Navigate(int32 camera, float32 yaw, float32 pitch, float32 zoom, bool reference)
{
    if (Camera_ != camera || Yaw_ != yaw || Pitch_ != pitch || Zoom_ != zoom || Reference_ != reference)
    {
        Camera_ = camera;
        CameraId_ = camera >= 0 && camera < Scene_.Cameras.size() ? Scene_.Cameras[camera].Id : QString();
        Yaw_ = yaw;
        Pitch_ = pitch;
        Zoom_ = zoom;
        Reference_ = reference;
        ViewDirty_ = true;
        if (Running())
        {
            Timer_.start();
        }
        Q_EMIT NavigationChanged();
    }
}
void RenderPreview::Resize(int32 width, int32 height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    const auto scale = math::Min(1.0F, 1024.0F / static_cast<float32>(math::Max(width, height)));
    RequestedWidth_ = math::Max(uint32{1}, static_cast<uint32>(static_cast<float32>(width) * scale));
    RequestedHeight_ = math::Max(uint32{1}, static_cast<uint32>(static_cast<float32>(height) * scale));
    if (Running())
    {
        Timer_.start();
    }
}
void RenderPreview::Orbit(int32 dx, int32 dy)
{
    auto yaw = Yaw_ + static_cast<float32>(dx) * .5F;
    while (yaw > 180)
    {
        yaw -= 360;
    }
    while (yaw < -180)
    {
        yaw += 360;
    }
    Navigate(-1, yaw, math::Clamp(Pitch_ + static_cast<float32>(dy) * .5F, -80.0F, 80.0F), Zoom_, Reference_);
}
void RenderPreview::Pan(int32 dx, int32 dy)
{
    const auto yaw = Yaw_ * .01745329252F, pitch = Pitch_ * .01745329252F;
    const math::Vector3 right{math::Cos(yaw), 0, -math::Sin(yaw)};
    const math::Vector3 up{-math::Sin(yaw) * math::Sin(pitch), math::Cos(pitch), -math::Cos(yaw) * math::Sin(pitch)};
    OrbitCenter_ = OrbitCenter_ +
                   (right * -static_cast<float32>(dx) + up * static_cast<float32>(dy)) * (OrbitRadius_ * Zoom_ * .002F);
    RestoredOrbit_ = true;
    Navigate(-1, Yaw_, Pitch_, Zoom_, Reference_);
    ViewDirty_ = true;
    if (Running())
    {
        Timer_.start();
    }
    Q_EMIT NavigationChanged();
}
void RenderPreview::Zoom(int32 steps)
{
    Navigate(-1, Yaw_, Pitch_, math::Clamp(Zoom_ - static_cast<float32>(steps) * .2F, 1.2F, 20.0F), Reference_);
}
QJsonObject RenderPreview::Navigation() const
{
    return {{QStringLiteral("yaw"), Yaw_},
            {QStringLiteral("pitch"), Pitch_},
            {QStringLiteral("zoom"), Zoom_},
            {QStringLiteral("camera"), CameraId_},
            {QStringLiteral("reference"), Reference_},
            {QStringLiteral("x"), OrbitCenter_.X},
            {QStringLiteral("y"), OrbitCenter_.Y},
            {QStringLiteral("z"), OrbitCenter_.Z},
            {QStringLiteral("radius"), OrbitRadius_}};
}
void RenderPreview::RestoreNavigation(const QJsonObject& state)
{
    const auto number = [&state](const QString& key, float32 fallback, float32 low, float32 high) {
        const auto value = state.value(key).toDouble(static_cast<float64>(fallback));
        return math::IsFinite(value) && value >= static_cast<float64>(low) && value <= static_cast<float64>(high)
                   ? static_cast<float32>(value)
                   : fallback;
    };
    Navigate(-1,
             number(QStringLiteral("yaw"), 25, -180, 180),
             number(QStringLiteral("pitch"), 20, -80, 80),
             number(QStringLiteral("zoom"), 3, 1.2F, 20),
             state.value(QStringLiteral("reference")).toBool());
    CameraId_ = state.value(QStringLiteral("camera")).toString().left(256);
    OrbitCenter_.X = number(QStringLiteral("x"), 0, -1000000, 1000000);
    OrbitCenter_.Y = number(QStringLiteral("y"), 0, -1000000, 1000000);
    OrbitCenter_.Z = number(QStringLiteral("z"), 0, -1000000, 1000000);
    OrbitRadius_ = number(QStringLiteral("radius"), 1, .000001F, 1000000);
    RestoredOrbit_ = state.value(QStringLiteral("radius")).isDouble() && state.value(QStringLiteral("x")).isDouble() &&
                     state.value(QStringLiteral("y")).isDouble() && state.value(QStringLiteral("z")).isDouble() &&
                     state.value(QStringLiteral("radius")).toDouble() > 0;
    if (!RestoredOrbit_)
    {
        OrbitCenter_ = Scene_.Center;
        OrbitRadius_ = Scene_.Radius;
    }
    ViewDirty_ = true;
    Q_EMIT NavigationChanged();
}
void RenderPreview::resetFrames() noexcept
{
    Q_EMIT ResettingFrames();
    if (mHasPendingFrame == true)
    {
        static_cast<void>(mViewport.Release(mPendingFrame));
    }
    mPendingFrame = {};
    mPendingImage = {};
    mHasPendingFrame = false;
    mHasFrame = false;
    mViewport.Reset();
    mHasViewport = false;
    mProducer = {};
    mIsAwaitingProducer = false;
}
bool RenderPreview::TakeFrame(rr::ViewportFrame& outFrame, rr::ViewportImage& outImage) noexcept
{
    if (mHasPendingFrame == false)
    {
        return false;
    }
    outFrame = mPendingFrame;
    outImage = mPendingImage;
    mPendingFrame = {};
    mPendingImage = {};
    mHasPendingFrame = false;
    return true;
}
void RenderPreview::ReleaseFrame(rr::ViewportFrame& inoutFrame) noexcept
{
    static_cast<void>(mViewport.Release(inoutFrame));
}
rhi::RasterStatus RenderPreview::PinDevice(rhi::NativeDeviceLease& outLease) noexcept
{
    return rhi::AcquireNativeDevice(Device_, outLease);
}
void RenderPreview::fail(const QString& message)
{
    Stop();
    Status_ = message;
    Q_EMIT Changed();
    Q_EMIT Failed(message);
}
bool RenderPreview::prepare()
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
        if (s == rhi::RasterStatus::Ready)
        {
            if (!LightingInitialized_)
            {
                const auto lighting = Renderer_.InitializeLighting(ludus::shaders::renderer_output::Vertex(),
                                                                   ludus::shaders::renderer_output::Fragment(),
                                                                   ludus::shaders::renderer_shadow::Fragment());
                if (!Accepted(lighting))
                {
                    return false;
                }
                LightingInitialized_ = true;
            }
            const auto lighting = Renderer_.PollLighting();
            if (lighting == rhi::RasterStatus::Pending)
            {
                return true;
            }
            if (lighting != rhi::RasterStatus::Ready)
            {
                return false;
            }
            return advance(Scene_.Lit == true
                               ? Renderer_.PrewarmLitProgram(ludus::shaders::renderer_lit::Vertex(),
                                                             ludus::shaders::renderer_lit::Fragment(),
                                                             ludus::shaders::renderer_lit_shadow::Fragment(),
                                                             Program_)
                               : Renderer_.PrewarmMaterialProgram(ludus::shaders::renderer_material::Vertex(),
                                                                  ludus::shaders::renderer_material::Fragment(),
                                                                  Program_));
        }
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
                rr::MeshDescription description;
                if (Scene_.Lit)
                {
                    const auto& source = Scene_.Definition->Meshes[static_cast<usize>(i)];
                    description.Positions = source.Positions.GetData();
                    description.VertexCount = source.Positions.GetSize();
                    description.Indices = source.Indices.GetData();
                    description.IndexCount = source.Indices.GetSize();
                    description.Texcoords = source.Texcoords.GetData();
                    description.Normals = source.Normals.GetData();
                }
                else
                {
                    description.Positions = mesh.Positions.constData();
                    description.VertexCount = static_cast<usize>(mesh.Positions.size());
                    description.Indices = mesh.Indices.constData();
                    description.IndexCount = static_cast<usize>(mesh.Indices.size());
                    description.Texcoords = mesh.Uvs.constData();
                    description.Normals = mesh.Normals.isEmpty() ? nullptr : mesh.Normals.constData();
                }
                if (!Accepted(Renderer_.CreateMesh(description, Meshes_[i])))
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
                                               .RetainCpuContent = false,
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
            if (surface.Texture >= 0)
            {
                description.Texture = Textures_[surface.Texture];
            }
            description.Tint = surface.Tint;
            rr::LitMaterialDescription lit;
            lit.Surface = description;
            lit.Surface.Sampler.Filter = rhi::RasterFilter::Linear;
            lit.Surface.Sampler.U = lit.Surface.Sampler.V =
                surface.Repeat ? rhi::RasterAddress::Repeat : rhi::RasterAddress::Clamp;
            lit.Metallic = surface.Metallic;
            lit.Roughness = surface.Roughness;
            lit.Emission = surface.Emission;
            rr::LightSelection selection;
            if (!Accepted(Scene_.Lit ? Renderer_.CreateLitMaterial(Program_, lit, Materials_[i], selection)
                                     : Renderer_.CreateMaterial(Program_, description, Materials_[i])))
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
            if (Scene_.Lit || object.Texture >= 0)
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
    if (Width_ == 0 || Height_ == 0 || mHasViewport == false)
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
        if (Accepted(mViewport.Update(Snapshot_, description, Report_)) == false)
        {
            return false;
        }
        ViewDirty_ = false;
        FreshReport_ = true;
        Phase_ = 6;
    }
    return Accepted(mViewport.Poll());
#else
    return false;
#endif
}
void RenderPreview::Tick()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (OwnsDevice_ == false)
    {
        return;
    }
    rhi::DeviceInfo device;
    const rhi::DeviceStatus ready = rhi::GetDeviceInfo(Device_, device);
    if (ready == rhi::DeviceStatus::Pending)
    {
        return;
    }
    if (ready == rhi::DeviceStatus::Ready && device.FrameOpen == true)
    {
        // RasterViewport retains recording when completion admission is full.
        // Retry that owned frame before lifetime service or new setup work.
        mProducer = {};
        const rhi::RasterStatus retried = mViewport.Render(Surface_, mProducer);
        rhi::DeviceInfo after;
        if (rhi::GetDeviceInfo(Device_, after) == rhi::DeviceStatus::Ready && after.FrameOpen == true)
        {
            return;
        }
        if (retried != rhi::RasterStatus::Ready)
        {
            fail(QStringLiteral("GPU viewport submission failed. World edits are preserved."));
            return;
        }
        mIsAwaitingProducer = true;
        return;
    }
    if (ready != rhi::DeviceStatus::Ready || rhi::PollLifetime(Device_) != rhi::RasterStatus::Ready)
    {
        fail(QStringLiteral("GPU device is unavailable. World edits are preserved."));
        return;
    }
    if (mIsAwaitingProducer == true)
    {
        const rhi::RasterStatus completion = rhi::GetStatus(Device_, mProducer);
        if (completion == rhi::RasterStatus::Pending)
        {
            return;
        }
        if (completion != rhi::RasterStatus::Ready)
        {
            fail(QStringLiteral("GPU viewport submission failed. World edits are preserved."));
            return;
        }
        if (mHasPendingFrame == true)
        {
            static_cast<void>(mViewport.Release(mPendingFrame));
            mHasPendingFrame = false;
        }
        if (mViewport.Acquire(mPendingFrame, mPendingImage) != rhi::RasterStatus::Ready)
        {
            fail(QStringLiteral("GPU viewport image is unavailable. World edits are preserved."));
            return;
        }
        mHasPendingFrame = true;
        mHasFrame = true;
        mIsAwaitingProducer = false;
        ++Frames_;
        const uint64 ownerFrame = Frames_;
        Q_EMIT FrameReady();
        // A receiver can stop/replace the preview at this transaction boundary.
        if (OwnsDevice_ == false || Frames_ != ownerFrame)
        {
            return;
        }
    }
    if (Phase_ >= 5 && (mHasViewport == false || Width_ != RequestedWidth_ || Height_ != RequestedHeight_))
    {
        resetFrames();
        Width_ = RequestedWidth_;
        Height_ = RequestedHeight_;
        const rhi::RasterStatus created = mViewport.Initialize(Renderer_,
                                                               Device_,
                                                               Width_,
                                                               Height_,
                                                               1,
                                                               Scene_.Lit == true ? rr::ViewportColorMode::PreferHdr
                                                                                  : rr::ViewportColorMode::Linear8);
        if (Accepted(created) == false)
        {
            fail(QStringLiteral("GPU viewport allocation failed. World edits are preserved."));
            return;
        }
        mHasViewport = true;
        ViewDirty_ = true;
    }
    if (prepare() == false)
    {
        fail(QStringLiteral("GPU renderer stopped or exceeded its supported profile. World edits are preserved."));
        return;
    }
    if (Phase_ != 6 || mHasViewport == false)
    {
        return;
    }
    const rhi::RasterStatus configured = mViewport.Poll();
    if (configured == rhi::RasterStatus::Pending)
    {
        return;
    }
    if (configured != rhi::RasterStatus::Ready)
    {
        fail(QStringLiteral("GPU viewport setup failed. World edits are preserved."));
        return;
    }
    mProducer = {};
    const rhi::RasterStatus rendered = mViewport.Render(Surface_, mProducer);
    if (rendered == rhi::RasterStatus::NotReady || rendered == rhi::RasterStatus::Pending)
    {
        return;
    }
    if (rendered != rhi::RasterStatus::Ready)
    {
        fail(QStringLiteral("GPU viewport draw failed (%1). World edits are preserved.")
                 .arg(static_cast<uint32>(rendered)));
        return;
    }
    mIsAwaitingProducer = true;
    if (FreshReport_ == true || Frames_ % 60 == 0)
    {
        FreshReport_ = false;
        Status_ = QStringLiteral("GPU raster viewport · %1×%2 · visible %3 · draws %4%5")
                      .arg(Width_)
                      .arg(Height_)
                      .arg(Report_.Visible)
                      .arg(Report_.Draws)
                      .arg(Scene_.Lit == true
                               ? QStringLiteral(" · authored material lighting; emissive indirect lighting unavailable")
                               : QString());
        Q_EMIT Changed();
    }
#endif
}
} // namespace ludus::editor
