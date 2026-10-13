#include "internal/readback.h"
#include "renderer_clustered_authored.h"
#include "renderer_clustered_authored_shadow.h"
#include "renderer_composite.h"
#include "renderer_flat.h"
#include "renderer_output.h"
#include "renderer_shadow.h"

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/time/time.hpp>
#include <ludus/graphics/renderer/renderer.hpp>
#include <ludus/graphics/renderer/views.hpp>
#include <ludus/graphics/rhi/device.h>
#include <ludus/graphics/rhi/lifetime.h>

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
using namespace ludus::foundation;
namespace rr = ludus::graphics::renderer;
namespace rhi = ludus::graphics::rhi;
namespace shaders = ludus::shaders;
using Status = rhi::RasterStatus;
static_assert(std::endian::native == std::endian::little, "Private acceptance fixtures encode little-endian words");
constexpr uint32 RESOURCE_SCENE_MAGIC = 0x5253434E;
constexpr uint32 RESOURCE_SCENE_VERSION = 1;
constexpr uint32 ACCEPTANCE_SIZE = 256;
constexpr usize ACCEPTANCE_CHANNELS = 4;
struct FixtureMaterial final
{
    rr::LitMaterialDescription Description;
    std::vector<uint8> Base;
    std::vector<uint8> Surface;
    std::vector<uint8> Emission;
    uint32 Dimensions[6]{};
};
struct FixtureMesh final
{
    std::vector<math::Vector3> Positions;
    std::vector<math::Vector3> Normals;
    std::vector<math::Vector2> Uvs;
    std::vector<math::Vector2> Uvs1;
    std::vector<math::Vector4> Tangents;
    std::vector<math::Vector4> Colors;
    std::vector<uint32> Indices;
    uint32 Material = 0;
    bool HasTangents = false;
};
struct FixtureItem final
{
    uint32 Mesh = 0;
    math::Affine3 Transform;
};
struct Fixture final
{
    std::vector<FixtureMaterial> Materials;
    std::vector<FixtureMesh> Meshes;
    std::vector<FixtureItem> Items;
    std::vector<math::Vector4> Environment;
    std::vector<rr::DirectLight> Lights;
    math::Matrix4 WorldToClip = math::Matrix4::Identity();
    math::Vector3 Eye{0, 0, 1};
    uint32 EnvironmentWidth = 0;
    uint32 EnvironmentHeight = 0;
};
struct Reader final
{
    std::ifstream Input;
    explicit Reader(const char* path) : Input(path, std::ios::binary)
    {
        REQUIRE(Input.good());
    }
    template <typename T>
    void Read(T& outValue)
    {
        Input.read(reinterpret_cast<char*>(&outValue), static_cast<std::streamsize>(sizeof(T)));
        REQUIRE(Input.good());
    }
    template <typename T>
    void ReadArray(std::vector<T>& outValues, usize count)
    {
        outValues.resize(count);
        Input.read(reinterpret_cast<char*>(outValues.data()), static_cast<std::streamsize>(count * sizeof(T)));
        REQUIRE(Input.good());
    }
};
Fixture Load(const char* path)
{
    Reader reader(path);
    uint32 header[8]{};
    reader.Read(header);
    REQUIRE(header[0] == RESOURCE_SCENE_MAGIC);
    REQUIRE(header[1] == RESOURCE_SCENE_VERSION);
    REQUIRE(header[2] <= 4096);
    REQUIRE(header[3] <= 512);
    REQUIRE(header[4] <= 8192);
    REQUIRE(header[5] <= 4096);
    REQUIRE(header[6] <= 4096);
    REQUIRE(header[7] <= 128);
    Fixture fixture;
    reader.Read(fixture.WorldToClip);
    reader.Read(fixture.Eye);
    fixture.EnvironmentWidth = header[5];
    fixture.EnvironmentHeight = header[6];
    reader.ReadArray(fixture.Environment, usize(header[5]) * header[6]);
    fixture.Lights.resize(header[7]);
    for (usize index = 0; index < fixture.Lights.size(); ++index)
    {
        rr::DirectLight& light = fixture.Lights[index];
        uint32 kind = 0;
        reader.Read(kind);
        light.SourceId = index + 1;
        light.Kind = static_cast<rr::LightKind>(kind);
        reader.Read(light.Color);
        reader.Read(light.Intensity);
        reader.Read(light.Position);
        reader.Read(light.Direction);
        reader.Read(light.Range);
        reader.Read(light.InnerCos);
        reader.Read(light.OuterCos);
    }
    fixture.Materials.resize(header[3]);
    for (FixtureMaterial& material : fixture.Materials)
    {
        reader.Read(material.Dimensions);
        uint32 alpha = 0;
        reader.Read(alpha);
        reader.Read(material.Description.MaterialUvSet);
        material.Description.Surface.Alpha = static_cast<rr::MaterialAlpha>(alpha);
        reader.Read(material.Description.Surface.Tint);
        reader.Read(material.Description.Metallic);
        reader.Read(material.Description.Roughness);
        reader.Read(material.Description.NormalScale);
        reader.Read(material.Description.Surface.AlphaCutoff);
        reader.Read(material.Description.Emission);
        reader.ReadArray(material.Base, usize(material.Dimensions[0]) * material.Dimensions[1] * 4);
        reader.ReadArray(material.Surface, usize(material.Dimensions[2]) * material.Dimensions[3] * 4);
        reader.ReadArray(material.Emission, usize(material.Dimensions[4]) * material.Dimensions[5] * 4);
    }
    fixture.Meshes.resize(header[2]);
    for (FixtureMesh& mesh : fixture.Meshes)
    {
        uint32 vertices = 0;
        uint32 indices = 0;
        uint32 hasTangents = 0;
        reader.Read(vertices);
        reader.Read(indices);
        reader.Read(mesh.Material);
        reader.Read(hasTangents);
        mesh.HasTangents = hasTangents != 0;
        REQUIRE(vertices <= 2000000);
        REQUIRE(indices <= 6000000);
        REQUIRE(mesh.Material < fixture.Materials.size());
        reader.ReadArray(mesh.Positions, vertices);
        reader.ReadArray(mesh.Normals, vertices);
        reader.ReadArray(mesh.Uvs, vertices);
        reader.ReadArray(mesh.Uvs1, vertices);
        reader.ReadArray(mesh.Tangents, vertices);
        reader.ReadArray(mesh.Colors, vertices);
        reader.ReadArray(mesh.Indices, indices);
    }
    fixture.Items.resize(header[4]);
    for (FixtureItem& item : fixture.Items)
    {
        reader.Read(item.Mesh);
        REQUIRE(item.Mesh < fixture.Meshes.size());
        reader.Read(item.Transform);
    }
    REQUIRE(reader.Input.peek() == std::char_traits<char>::eof());
    return fixture;
}
Fixture BuildSynthetic()
{
    Fixture fixture;
    fixture.EnvironmentWidth = 64;
    fixture.EnvironmentHeight = 32;
    fixture.Environment.resize(usize{64} * 32, {8, 8, 8, 1});
    FixtureMaterial material;
    material.Dimensions[0] = material.Dimensions[1] = 2;
    material.Dimensions[2] = material.Dimensions[3] = material.Dimensions[4] = material.Dimensions[5] = 1;
    material.Base = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    material.Surface = {128, 128, 255, 255};
    material.Emission = {255, 255, 255, 255};
    material.Description.MaterialUvSet = 1;
    material.Description.Roughness = 1;
    fixture.Materials.push_back(material);
    FixtureMesh mesh;
    mesh.Positions = {{-.8F, -.8F, .5F}, {.8F, -.8F, .5F}, {.8F, .8F, .5F}, {-.8F, .8F, .5F}};
    mesh.Normals.resize(4, {0, 0, 1});
    mesh.Tangents.resize(4, {1, 0, 0, 1});
    mesh.Colors.resize(4, {.25F, 1, .5F, 1});
    mesh.Uvs.resize(4, {.25F, .25F});
    mesh.Uvs1.resize(4, {.75F, .25F});
    mesh.Indices = {0, 1, 2, 0, 2, 3};
    mesh.HasTangents = true;
    fixture.Meshes.push_back(mesh);
    fixture.Items.push_back({});
    return fixture;
}
void RenderFixture(const Fixture& fixture, bool isSynthetic)
{
    const uint64 preparationStart = time::NowTicks();
    rhi::Shutdown();
    struct Guard final
    {
        ~Guard()
        {
            rhi::Shutdown();
        }
    } guard;
    rhi::DeviceDescription description;
    description.Required.PortableRaster = true;
    description.RequiredProfile.Raster.MinBindings = 11;
    description.Storage.ResourcesPerKind = 4096;
    description.Storage.BindingDescriptors = 8192;
    description.Storage.DrawsPerFrame = 8192;
    description.Storage.ResourceBytes = uint64{4} * 1024 * 1024 * 1024;
    description.Storage.HostBytes = usize{256} * 1024 * 1024;
    rhi::DeviceHandle device;
    rhi::SurfaceHandle surface;
    REQUIRE(
        rhi::CreateDevice({}, { .Width = ACCEPTANCE_SIZE, .Height = ACCEPTANCE_SIZE }, description, device, surface) ==
        rhi::DeviceStatus::Ready);
    rr::Limits limits;
    limits.Meshes = static_cast<uint32>(fixture.Meshes.size() + 4);
    limits.Materials = static_cast<uint32>(fixture.Materials.size() + 4);
    limits.Objects = static_cast<uint32>(fixture.Items.size() + 4);
    rr::Renderer renderer;
    REQUIRE(renderer.Initialize(device, shaders::renderer_flat::Vertex(), shaders::renderer_flat::Fragment(), limits) ==
            Status::Ready);
    REQUIRE(renderer.InitializeViews(shaders::renderer_composite::Vertex(), shaders::renderer_composite::Fragment()) ==
            Status::Ready);
    REQUIRE(renderer.InitializeLighting(shaders::renderer_output::Vertex(),
                                        shaders::renderer_output::Fragment(),
                                        shaders::renderer_shadow::Fragment()) == Status::Ready);
    const auto wait = [&renderer](const auto& handle) {
        const uint64 start = time::NowTicks();
        Status status = renderer.GetStatus(handle);
        while (status == Status::Pending && time::NowTicks() - start < 30 * time::NANOSECONDS_PER_SECOND)
        {
            status = renderer.GetStatus(handle);
        }
        REQUIRE(status == Status::Ready);
    };
    rr::MaterialProgram program;
    const Status warming = renderer.PrewarmLitProgram(shaders::renderer_clustered_authored::Vertex(),
                                                      shaders::renderer_clustered_authored::Fragment(),
                                                      shaders::renderer_clustered_authored_shadow::Fragment(),
                                                      program);
    REQUIRE((warming == Status::Ready || warming == Status::Pending));
    wait(program);
    rr::EnvironmentMap environment;
    rr::EnvironmentDescription hdr;
    hdr.Pixels = fixture.Environment.data();
    hdr.Width = fixture.EnvironmentWidth;
    hdr.Height = fixture.EnvironmentHeight;
    hdr.CpuByteBudget = usize{512} * 1024 * 1024;
    const Status upload = environment.Initialize(device, hdr);
    REQUIRE((upload == Status::Ready || upload == Status::Pending));
    const uint64 environmentStart = time::NowTicks();
    while (environment.GetStatus() == Status::Pending &&
           time::NowTicks() - environmentStart < 30 * time::NANOSECONDS_PER_SECOND)
    {
    }
    REQUIRE(environment.GetStatus() == Status::Ready);
    std::vector<rr::MaterialVersion> materials(fixture.Materials.size());
    rr::LightClusterDescription clusters;
    clusters.Bounds = {{-1000000, -1000000, -1000000}, {1000000, 1000000, 1000000}};
    for (usize index = 0; index < fixture.Materials.size(); ++index)
    {
        const FixtureMaterial& input = fixture.Materials[index];
        rr::LitMaterialDescription material = input.Description;
        rhi::TextureHandle textures[3]{};
        const std::vector<uint8>* bytes[]{&input.Base, &input.Surface, &input.Emission};
        for (usize texture = 0; texture < 3; ++texture)
        {
            const uint32 width = input.Dimensions[texture * 2];
            const uint32 height = input.Dimensions[texture * 2 + 1];
            REQUIRE(rhi::CreateTexture(
                        device,
                        {width, height, texture == 1 ? rhi::RasterFormat::Rgba8Unorm : rhi::RasterFormat::Rgba8Srgb},
                        {{bytes[texture]->data(), bytes[texture]->size()}, usize(width) * 4},
                        textures[texture]) == Status::Ready);
        }
        material.Surface.Texture = textures[0];
        material.SurfaceMap = textures[1];
        material.EmissionMap = textures[2];
        material.AreAuthoredInputsEnabled = true;
        material.Environment = &environment;
        material.Clusters = &clusters;
        material.Lights = fixture.Lights.data();
        material.LightCount = fixture.Lights.size();
        material.Surface.Sampler.Filter = rhi::RasterFilter::Linear;
        material.Surface.Sampler.U = material.Surface.Sampler.V = rhi::RasterAddress::Repeat;
        material.Surface.Lod = 0;
        rr::LightSelection selected;
        const Status created = renderer.CreateLitMaterial(program, material, materials[index], selected);
        REQUIRE((created == Status::Ready || created == Status::Pending));
        CHECK(selected.Count == fixture.Lights.size());
        CHECK(selected.Dropped == 0);
        wait(materials[index]);
        for (rhi::TextureHandle texture : textures)
        {
            REQUIRE(rhi::Destroy(device, texture) == Status::Ready);
        }
    }
    environment.Reset(); // Materials must retain source data, prefiltered texture and sampler.
    std::vector<rr::Mesh> meshes(fixture.Meshes.size());
    for (usize index = 0; index < fixture.Meshes.size(); ++index)
    {
        const FixtureMesh& input = fixture.Meshes[index];
        rr::MeshDescription geometry{input.Positions.data(),
                                     input.Positions.size(),
                                     input.Indices.data(),
                                     input.Indices.size()};
        geometry.Normals = input.Normals.data();
        geometry.Texcoords = input.Uvs.data();
        geometry.MaterialTexcoords1 = input.Uvs1.data();
        geometry.Tangents = input.HasTangents ? input.Tangents.data() : nullptr;
        geometry.Colors = input.Colors.data();
        const Status created = renderer.CreateMesh(geometry, meshes[index]);
        REQUIRE((created == Status::Ready || created == Status::Pending));
        wait(meshes[index]);
    }
    std::vector<rr::SceneItem> items(fixture.Items.size());
    for (usize index = 0; index < fixture.Items.size(); ++index)
    {
        const FixtureItem& input = fixture.Items[index];
        items[index].Geometry = meshes[input.Mesh];
        items[index].Surface = materials[fixture.Meshes[input.Mesh].Material];
        items[index].Transform = input.Transform;
        items[index].SourceId = index + 1;
        items[index].Pass = fixture.Materials[fixture.Meshes[input.Mesh].Material].Description.Surface.Alpha ==
                                    rr::MaterialAlpha::Painter
                                ? rr::Layer::Overlay
                                : rr::Layer::Opaque;
    }
    rr::Snapshot snapshot;
    REQUIRE(renderer.CreateSnapshot(items.data(), items.size(), snapshot) == Status::Ready);
    rhi::TextureHandle target;
    REQUIRE(rhi::CreateTexture(device,
                               {ACCEPTANCE_SIZE, ACCEPTANCE_SIZE, rhi::RasterFormat::Rgba16Float, true},
                               {},
                               target) == Status::Ready);
    rr::ViewMapping mapping;
    REQUIRE(rr::ResolveViewMapping({ACCEPTANCE_SIZE, ACCEPTANCE_SIZE, rr::ScreenFit::Fill},
                                   {0, 0, ACCEPTANCE_SIZE, ACCEPTANCE_SIZE},
                                   ACCEPTANCE_SIZE,
                                   ACCEPTANCE_SIZE,
                                   mapping) == Status::Ready);
    rr::Presentation presentation;
    const Status composed = renderer.PrepareLightingPresentation(target, mapping, {}, presentation);
    REQUIRE((composed == Status::Ready || composed == Status::Pending));
    wait(presentation);
    const uint64 preparationNanoseconds = time::NowTicks() - preparationStart;
    std::vector<uint8> images[2];
    rr::ViewReport reports[2];
    uint64 frameNanoseconds[2]{};
    for (usize variant = 0; variant < 2; ++variant)
    {
        rr::ViewDescription viewDescription;
        viewDescription.WorldToClip = fixture.WorldToClip;
        viewDescription.CameraPosition = fixture.Eye;
        viewDescription.Reference = variant == 1;
        viewDescription.DirectLighting = variant == 1;
        rr::PreparedView view;
        rr::ViewReport report;
        const Status prepared = renderer.PrepareView(snapshot, viewDescription, view, report);
        REQUIRE((prepared == Status::Ready || prepared == Status::Pending));
        wait(view);
        reports[variant] = report;
        CHECK(report.Submitted == items.size());
        CHECK(report.Visible + report.Culled == items.size());
        INFO("submitted=" << report.Submitted << " visible=" << report.Visible << " draws=" << report.Draws);
        const uint64 frameStart = time::NowTicks();
        REQUIRE(rhi::BeginFrame(device, surface) == rhi::DeviceStatus::Ready);
        rhi::RasterPassDescription pass;
        pass.Color = target;
        REQUIRE(renderer.DrawView(view, pass) == Status::Ready);
        REQUIRE(renderer.DrawLightingPresentation(presentation) == Status::Ready);
        rhi::SubmissionToken completion;
        REQUIRE(rhi::EndFrame(device, completion) == Status::Ready);
        images[variant].resize(usize{ACCEPTANCE_SIZE} * ACCEPTANCE_SIZE * ACCEPTANCE_CHANNELS);
        REQUIRE(rhi::backend::ReadHeadlessPixels({images[variant].data(), images[variant].size()}));
        frameNanoseconds[variant] = time::NowTicks() - frameStart;
        REQUIRE(renderer.Release(view) == Status::Ready);
    }
    usize changed = 0;
    usize nonblack = 0;
    usize magenta = 0;
    int32 maximum = 0;
    for (usize pixel = 0; pixel < images[0].size(); pixel += 4)
    {
        bool isChanged = false;
        for (usize channel = 0; channel < 3; ++channel)
        {
            const int32 difference = static_cast<int32>(images[0][pixel + channel]) - images[1][pixel + channel];
            const int32 absolute = difference < 0 ? -difference : difference;
            maximum = math::Max(maximum, absolute);
            isChanged = isChanged || absolute > 1;
        }
        changed += isChanged ? 1 : 0;
        nonblack += (images[0][pixel] + images[0][pixel + 1] + images[0][pixel + 2]) > 0 ? 1 : 0;
        magenta += images[0][pixel] == 255 && images[0][pixel + 1] == 0 && images[0][pixel + 2] == 255 ? 1 : 0;
    }
    CHECK(nonblack > 100);
    CHECK(magenta == 0);
    CHECK(changed == 0);
    if (isSynthetic)
    {
        const usize center = (usize{ACCEPTANCE_SIZE / 2} * ACCEPTANCE_SIZE + ACCEPTANCE_SIZE / 2) * 4;
        CHECK(images[0][center + 1] > images[0][center] + 50);
        CHECK(images[0][center + 1] > images[0][center + 2] + 50);
    }
    const char* outputPath = std::getenv("LUDUS_RESOURCE_SCENE_OUTPUT");
    if (outputPath != nullptr)
    {
        std::ofstream output(outputPath, std::ios::binary);
        REQUIRE(output.good());
        output << "P6\n" << ACCEPTANCE_SIZE << " " << ACCEPTANCE_SIZE << "\n255\n";
        for (usize pixel = 0; pixel < images[0].size(); pixel += 4)
        {
            output.write(reinterpret_cast<const char*>(images[0].data() + pixel), 3);
        }
        REQUIRE(output.good());
        rhi::StorageUsage usage;
        REQUIRE(rhi::GetStorageUsage(device, usage) == Status::Ready);
        std::ofstream metrics(std::string(outputPath) + ".json");
        metrics << "{\"renderer_host_bytes\":" << renderer.GetHostBytes()
                << ",\"rhi_peak_host_bytes\":" << usage.PeakHostBytes
                << ",\"rhi_peak_resource_bytes\":" << usage.PeakResourceBytes
                << ",\"preparation_ns\":" << preparationNanoseconds << ",\"frame_and_readback_ns\":["
                << frameNanoseconds[0] << "," << frameNanoseconds[1] << "],\"draws\":[" << reports[0].Draws << ","
                << reports[1].Draws << "],\"submitted\":" << reports[0].Submitted << ",";

        metrics << "\"nonblack\":" << nonblack << ",\"magenta\":" << magenta << ",\"changed_pixels\":" << changed
                << ",\"maximum_channel_error\":" << maximum << "}\n";
    }
}
} // namespace
TEST_CASE("Authored UV/color/tangents and retained HDR environment shade on native GPU", "[renderer][gpu][authored]")
{
    RenderFixture(BuildSynthetic(), true);
}
TEST_CASE("Full static resource scenes retain every primitive/material and match direct raster",
          "[.resources][renderer][gpu][authored]")
{
    const char* sourcePath = std::getenv("LUDUS_RESOURCE_SCENE_FIXTURE");
    REQUIRE(sourcePath != nullptr);
    RenderFixture(Load(sourcePath), false);
}
