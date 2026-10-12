#include <ludus/foundation/base/byte_order.hpp>
#include <ludus/foundation/base/parse_number.hpp>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/filesystem/persistence.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/time/time.hpp>

#include <ludus/content/content.h>
#include <ludus/graphics/renderer/renderer.hpp>

#include <cstring>
#include <string_view>

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

// Private versioned sample format; never casts serialized bytes to native objects.
class Reader final
{
public:
    Reader(const uint8* data, usize size) noexcept : mData(data), mSize(size) {}
    bool Read(uint32& value) noexcept
    {
        if (!core::TryReadLittleEndian<uint32>({mData, mSize}, value))
        {
            return false;
        }
        mData += 4;
        mSize -= 4;
        return true;
    }
    bool Read(float32& value) noexcept
    {
        uint32 bits = 0;
        if (!Read(bits))
        {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return math::IsFinite(value);
    }
    bool Read(math::Vector3& value) noexcept
    {
        return Read(value.X) && Read(value.Y) && Read(value.Z);
    }
    const uint8* Take(usize size) noexcept
    {
        if (size > mSize)
        {
            return nullptr;
        }
        const auto* result = mData;
        mData += size;
        mSize -= size;
        return result;
    }
    [[nodiscard]] usize Remaining() const noexcept
    {
        return mSize;
    }

private:
    const uint8* mData;
    usize mSize;
};

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

bool Load(Reader& reader,
          rr::Renderer& renderer,
          rhi::DeviceHandle device,
          rr::MaterialProgram program,
          rr::Snapshot& snapshot,
          rr::ReferenceCaptureDescription& capture) noexcept
{
    uint32 version = 0, meshCount = 0, materialCount = 0, textureCount = 0, itemCount = 0;
    const auto* magic = reader.Take(8);
    if (!magic || std::memcmp(magic, "LUSCN01\0", 8) != 0 || !reader.Read(version) || version != 1 ||
        !reader.Read(meshCount) || meshCount != 6 || !reader.Read(materialCount) || materialCount != 8 ||
        !reader.Read(textureCount) || textureCount != 3 || !reader.Read(itemCount) || itemCount == 0 || itemCount > 256)
    {
        return false;
    }
    math::Vector3 eye, target, up;
    float32 fov = 0;
    if (!reader.Read(eye) || !reader.Read(target) || !reader.Read(up) || !reader.Read(fov))
    {
        return false;
    }
    math::Matrix4 view, projection;
    if (math::TryLookAt(eye, target, up, view) != math::MathStatus::Success ||
        math::TryPerspectiveReverseZ(fov, 1, .05F, 100, projection) != math::MathStatus::Success)
    {
        return false;
    }
    capture.WorldToClip = projection * view;
    capture.CameraPosition = eye;
    capture.ReverseZ = true;
    rhi::TextureHandle textures[3];
    for (auto& texture : textures)
    {
        uint32 width = 0, height = 0, size = 0;
        if (!reader.Read(width) || !reader.Read(height) || !reader.Read(size) || width == 0 || height == 0 ||
            width > 4096 || height > 4096 || size != usize(width) * height * 4)
        {
            return false;
        }
        const auto* bytes = reader.Take(size);
        if (!bytes)
        {
            return false;
        }
        rhi::TextureDescription description;
        description.Width = width;
        description.Height = height;
        description.Format = rhi::RasterFormat::Rgba8Srgb;
        description.RetainCpuContent = true;
        if (rhi::CreateTexture(device, description, {{bytes, size}, usize(width) * 4}, texture) != Status::Ready)
        {
            return false;
        }
    }
    rr::MaterialVersion materials[8];
    for (auto& material : materials)
    {
        uint32 texture = 0;
        rr::LitMaterialDescription description;
        if (!reader.Read(texture) || !reader.Read(description.Metallic) || !reader.Read(description.Roughness) ||
            !reader.Read(description.Emission) || (texture >= 3 && texture != 0xffffffffU))
        {
            return false;
        }
        if (texture < 3)
        {
            description.Surface.Texture = textures[texture];
            description.Surface.Sampler.Filter = rhi::RasterFilter::Linear;
            description.Surface.Sampler.U = description.Surface.Sampler.V =
                texture == 0 ? rhi::RasterAddress::Clamp : rhi::RasterAddress::Repeat;
        }
        else
        {
            description.Surface.Tint = {0, 0, 0, 1};
        }
        rr::LightSelection selection;
        if (Wait(renderer, material, renderer.CreateLitMaterial(program, description, material, selection)) !=
            Status::Ready)
        {
            return false;
        }
    }
    rr::Mesh meshes[6];
    for (auto& mesh : meshes)
    {
        uint32 vertexCount = 0, indexCount = 0;
        if (!reader.Read(vertexCount) || !reader.Read(indexCount) || vertexCount == 0 || vertexCount > 1000000 ||
            indexCount == 0 || indexCount > 3000000 || indexCount % 3 != 0 ||
            usize(vertexCount) * 32 + usize(indexCount) * 4 > reader.Remaining())
        {
            return false;
        }
        Array<math::Vector3> positions, normals;
        Array<math::Vector2> uvs;
        Array<uint32> indices;
        if (!positions.TryResize(vertexCount) || !normals.TryResize(vertexCount) || !uvs.TryResize(vertexCount) ||
            !indices.TryResize(indexCount))
        {
            return false;
        }
        for (usize i = 0; i < vertexCount; ++i)
        {
            if (!reader.Read(positions[i]) || !reader.Read(normals[i]) || !reader.Read(uvs[i].X) ||
                !reader.Read(uvs[i].Y))
            {
                return false;
            }
        }
        for (auto& index : indices)
        {
            if (!reader.Read(index) || index >= vertexCount)
            {
                return false;
            }
        }
        rr::MeshDescription description{positions.GetData(), vertexCount, indices.GetData(), indexCount};
        description.Normals = normals.GetData();
        description.Texcoords = uvs.GetData();
        if (Wait(renderer, mesh, renderer.CreateMesh(description, mesh)) != Status::Ready)
        {
            return false;
        }
    }
    Array<rr::SceneItem> items;
    if (!items.TryResize(itemCount))
    {
        return false;
    }
    for (usize i = 0; i < itemCount; ++i)
    {
        uint32 mesh = 0, material = 0;
        auto& item = items[i];
        if (!reader.Read(mesh) || !reader.Read(material) || mesh >= meshCount || material >= materialCount)
        {
            return false;
        }
        for (auto& column : item.Transform.Columns)
        {
            if (!reader.Read(column))
            {
                return false;
            }
        }
        if (!reader.Read(item.Transform.Translation))
        {
            return false;
        }
        item.Geometry = meshes[mesh];
        item.Surface = materials[material];
        item.SourceId = i + 1;
    }
    if (reader.Remaining() != 0 || renderer.CreateSnapshot(items.GetData(), items.GetSize(), snapshot) != Status::Ready)
    {
        return false;
    }
    for (auto mesh : meshes)
    {
        static_cast<void>(renderer.Release(mesh));
    }
    for (auto material : materials)
    {
        static_cast<void>(renderer.Release(material));
    }
    for (auto texture : textures)
    {
        static_cast<void>(rhi::Destroy(device, texture));
    }
    return true;
}

bool Number(std::string_view text, uint32 maximum, uint32& output) noexcept
{
    uint64 value = 0;
    if (ParseUint64(text.data(), text.size(), value) != NumberParseStatus::Success || value == 0 || value > maximum)
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
    Reader reader(bundle.Data().data(), bundle.Data().size());
    rr::Snapshot snapshot;
    if (!Load(reader, renderer, device, program, snapshot, description))
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
