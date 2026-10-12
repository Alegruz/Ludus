#include <ludus/foundation/base/checked_integer.hpp>
#include <ludus/foundation/base/parse_number.hpp>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/filesystem/persistence.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/time/time.hpp>

#include <ludus/content/content.h>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/runtime/resources/scene.h>

#include "ray_query.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include "renderer_lit.h"
#include "renderer_lit_shadow.h"
#include "renderer_output.h"
#include "renderer_shadow.h"

namespace
{
using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
using Status = rhi::RasterStatus;
LUDUS_DEFINE_LOG_CATEGORY(LOG_SCENE, "ReferenceScene");

template <typename Handle>
Status Wait(rr::Renderer& renderer, const Handle& handle, Status status) noexcept
{
    const auto started = time::NowTicks();
    while (status == Status::Pending && time::NowTicks() - started < 10 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.GetStatus(handle);
    }
    return status;
}

bool Load(const uint8* bytes,
          usize count,
          rr::Renderer& renderer,
          rhi::DeviceHandle device,
          rr::MaterialProgram program,
          rr::Snapshot& snapshot,
          rr::ReferenceCaptureDescription& capture) noexcept
{
    ludus::resources::SceneDefinition scene;
    if (ludus::resources::SceneDefinition::Decode(bytes, count, scene) != ludus::resources::Status::Ok)
    {
        return false;
    }
    math::Matrix4 view, projection;
    if (math::TryLookAt(scene.Eye, scene.Target, scene.Up, view) != math::MathStatus::Success ||
        math::TryPerspectiveReverseZ(scene.VerticalFov, 1, .05F, 100, projection) != math::MathStatus::Success)
    {
        return false;
    }
    capture.WorldToClip = projection * view;
    capture.CameraPosition = scene.Eye;
    capture.ReverseZ = true;
    rhi::TextureHandle textures[8];
    for (usize i = 0; i < scene.Textures.GetSize(); ++i)
    {
        const auto& source = scene.Textures[i];
        rhi::TextureDescription description;
        description.Width = source.Width;
        description.Height = source.Height;
        description.Format = rhi::RasterFormat::Rgba8Srgb;
        description.RetainCpuContent = true;
        if (rhi::CreateTexture(device,
                               description,
                               {{source.Pixels.GetData(), source.Pixels.GetSize()}, usize(source.Width) * 4},
                               textures[i]) != Status::Ready)
        {
            return false;
        }
    }
    rr::MaterialVersion materials[8];
    for (usize i = 0; i < scene.Materials.GetSize(); ++i)
    {
        const auto& source = scene.Materials[i];
        rr::LitMaterialDescription description;
        description.Metallic = source.Metallic;
        description.Roughness = source.Roughness;
        description.Emission = source.Emission;
        if (source.Texture != ~uint32{0})
        {
            description.Surface.Texture = textures[source.Texture];
            description.Surface.Sampler.Filter = rhi::RasterFilter::Linear;
            description.Surface.Sampler.U = description.Surface.Sampler.V =
                source.Repeat ? rhi::RasterAddress::Repeat : rhi::RasterAddress::Clamp;
        }
        else
        {
            description.Surface.Tint = {0, 0, 0, 1};
        }
        rr::LightSelection selection;
        if (Wait(renderer, materials[i], renderer.CreateLitMaterial(program, description, materials[i], selection)) !=
            Status::Ready)
        {
            return false;
        }
    }
    rr::Mesh meshes[6];
    for (usize i = 0; i < scene.Meshes.GetSize(); ++i)
    {
        const auto& source = scene.Meshes[i];
        rr::MeshDescription description{source.Positions.GetData(),
                                        source.Positions.GetSize(),
                                        source.Indices.GetData(),
                                        source.Indices.GetSize()};
        description.Normals = source.Normals.GetData();
        description.Texcoords = source.Texcoords.GetData();
        if (Wait(renderer, meshes[i], renderer.CreateMesh(description, meshes[i])) != Status::Ready)
        {
            return false;
        }
    }
    Array<rr::SceneItem> items;
    if (!items.TryResize(scene.Objects.GetSize()))
    {
        return false;
    }
    for (usize i = 0; i < scene.Objects.GetSize(); ++i)
    {
        const auto& source = scene.Objects[i];
        auto& item = items[i];
        item.Geometry = meshes[source.Mesh];
        item.Surface = materials[source.Material];
        item.Transform = source.Transform;
        item.SourceId = i + 1;
    }
    if (renderer.CreateSnapshot(items.GetData(), items.GetSize(), snapshot) != Status::Ready)
    {
        return false;
    }
    for (usize i = 0; i < scene.Meshes.GetSize(); ++i)
    {
        (void)renderer.Release(meshes[i]);
    }
    for (usize i = 0; i < scene.Materials.GetSize(); ++i)
    {
        (void)renderer.Release(materials[i]);
    }
    return true;
}

bool Number(const char* text, uint32 maximum, uint32& output) noexcept
{
    usize size = 0;
    while (size <= 1024 && text[size] != '\0')
    {
        ++size;
    }
    if (size > 1024)
    {
        return false;
    }
    uint64 value = 0;
    if (ParseUint64(text, size, value) != NumberParseStatus::Success || value == 0 || value > maximum)
    {
        return false;
    }
    return core::TryIntegerCast(value, output);
}
} // namespace

int Run(int argc, char** argv) noexcept
{
    if (argc != 5)
    {
        return 1; // root-directory, output-directory, square extent, samples
    }
    rr::ReferenceCaptureDescription description;
    if (!Number(argv[3], 1024, description.Width) || !Number(argv[4], 4096, description.TargetSamples))
    {
        return 2;
    }
    description.Height = description.Width;
    description.MaxDepth = 4;
    description.RouletteDepth = 3;
    description.Seed = 871;
    ludus::content::Bytes bundle;
    if (ludus::content::ReadFile(argv[1], "scene.luscn", usize{64} * 1024 * 1024, bundle) != ludus::content::Status::Ok)
    {
        return 3;
    }
    rhi::DeviceDescription request;
    request.Required.PortableRaster = true;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    if (rhi::CreateDevice({}, { .Width = 8, .Height = 8 }, request, device, surface) != rhi::DeviceStatus::Ready)
    {
        return 4;
    }
    struct Shutdown final
    {
        ~Shutdown() noexcept
        {
            rhi::Shutdown();
        }
    } shutdown;
    rr::Renderer renderer;
    if (renderer.Initialize(device,
                            ludus::shaders::renderer_flat::Vertex(),
                            ludus::shaders::renderer_flat::Fragment()) != Status::Ready ||
        renderer.InitializeViews(ludus::shaders::renderer_composite::Vertex(),
                                 ludus::shaders::renderer_composite::Fragment()) != Status::Ready ||
        renderer.InitializeLighting(ludus::shaders::renderer_output::Vertex(),
                                    ludus::shaders::renderer_output::Fragment(),
                                    ludus::shaders::renderer_shadow::Fragment()) != Status::Ready)
    {
        return 5;
    }
    const rr::ReferenceRenderingOptions options{rr::ReferenceTraversal::CpuOracle,
                                                usize{128} * 1024 * 1024,
                                                usize{128} * 1024 * 1024,
                                                512};
    if (renderer.EnableReferenceRendering(ludus::shaders::ray_query::Query(), options) != Status::Ready)
    {
        return 6;
    }
    rr::MaterialProgram program;
    if (Wait(renderer,
             program,
             renderer.PrewarmLitProgram(ludus::shaders::renderer_lit::Vertex(),
                                        ludus::shaders::renderer_lit::Fragment(),
                                        ludus::shaders::renderer_lit_shadow::Fragment(),
                                        program)) != Status::Ready)
    {
        return 7;
    }
    rr::Snapshot snapshot;
    if (!Load(bundle.Data().data(), bundle.Data().size(), renderer, device, program, snapshot, description))
    {
        return 8;
    }
    rr::ReferenceCapture capture;
    auto status = renderer.BeginReferenceCapture(snapshot, description, capture);
    if (status != Status::Ready && status != Status::Pending)
    {
        return 9;
    }
    const auto started = time::NowTicks();
    status = Status::Pending;
    while (status == Status::Pending && time::NowTicks() - started < 1800 * time::NANOSECONDS_PER_SECOND)
    {
        status = renderer.AdvanceReferenceCapture(capture);
    }
    if (status != Status::Ready)
    {
        return 10;
    }
    rr::ReferenceCaptureInfo info;
    if (renderer.GetReferenceCaptureInfo(capture, info) != Status::Ready || !info.Complete ||
        info.InvalidContributions != 0)
    {
        return 11;
    }
    Array<uint8> bytes;
    if (!bytes.TryResize(256 + usize(description.Width) * description.Height * 16))
    {
        return 12;
    }
    usize written = 0;
    if (renderer.EncodeReferenceHdr(capture, bytes.GetData(), bytes.GetSize(), written) != Status::Ready)
    {
        return 13;
    }
    filesystem::WriteDirectory directory;
    if (!directory.Open(argv[2]).Succeeded() ||
        !directory.Publish("capture.luhdr", {bytes.GetData(), written}).Outcome.Succeeded())
    {
        return 14;
    }
    return 0;
}

int main(int argc, char** argv)
{
    using namespace ludus::foundation::logging;
    LogConfig config;
    config.EnableFile = false;
    config.EnableDebugger = false;
    LogSystem::Initialize(config);
    const int result = Run(argc, argv);
    if (result != 0)
    {
        LUDUS_LOG_ERROR(LOG_SCENE,
                        "Scene capture failed at step {}. Usage: reference_scene <cooked-directory> <output-directory> "
                        "<extent:1..1024> <samples:1..4096>. See the reference-scene guide for step meanings.",
                        result);
    }
    else
    {
        LUDUS_LOG_INFO(LOG_SCENE, "Complete CPU-oracle capture written to capture.luhdr.");
    }
    LogSystem::Shutdown();
    return result;
}
