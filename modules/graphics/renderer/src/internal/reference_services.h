#pragma once
// Included only by renderer.cpp: captured jobs retain the existing snapshot,
// mesh and material incarnations, while RHI retains accepted native work.

#include <ludus/foundation/base/byte_order.hpp>
#include <ludus/foundation/base/checked_integer.hpp>
#include <ludus/foundation/hash/hash.hpp>

#include <cstring>

namespace ludus::graphics::renderer
{
namespace
{
using internal::ReferenceJob;
using internal::ReferenceStage;
using internal::ReferenceTriangle;
Status ReferenceAdmission(rhi::DeviceHandle device, bool mutation = true) noexcept
{
    rhi::DeviceInfo info;
    switch (rhi::GetDeviceInfo(device, info))
    {
        case rhi::DeviceStatus::Ready:
            return mutation && info.FrameOpen ? Status::InvalidState : Status::Ready;
        case rhi::DeviceStatus::InvalidHandle:
            return Status::InvalidHandle;
        case rhi::DeviceStatus::DeviceLost:
            return Status::DeviceLost;
        case rhi::DeviceStatus::Pending:
            return Status::NotReady;
        default:
            return Status::Failed;
    }
}
bool SameReferenceLight(const DirectLight& a, const DirectLight& b) noexcept
{
    return a.SourceId == b.SourceId && a.Kind == b.Kind && a.Color == b.Color && a.Intensity == b.Intensity &&
           a.Position == b.Position && a.Direction == b.Direction && a.Range == b.Range && a.InnerCos == b.InnerCos &&
           a.OuterCos == b.OuterCos;
}
bool SameReferenceIntegral(const ReferenceJob& a,
                           const ReferenceCaptureDescription& b,
                           const LightSelection& lights) noexcept
{
    const auto& d = a.Description;
    if (d.WorldToClip != b.WorldToClip || d.CameraPosition != b.CameraPosition ||
        d.OrthographicCamera != b.OrthographicCamera || d.ReverseZ != b.ReverseZ || d.InfiniteFar != b.InfiniteFar ||
        d.Width != b.Width || d.Height != b.Height || d.MaxDepth != b.MaxDepth || d.RouletteDepth != b.RouletteDepth ||
        d.SecondaryDistance != b.SecondaryDistance || d.Environment != b.Environment || d.TextureMip != b.TextureMip ||
        d.Seed != b.Seed || d.CenterRays != b.CenterRays || d.LightCount != lights.Count)
    {
        return false;
    }
    for (usize i = 0; i < lights.Count; ++i)
    {
        if (!SameReferenceLight(a.Lights[i], lights.Lights[i]))
        {
            return false;
        }
    }
    return true;
}
Status ValidateReferenceDescription(const ReferenceCaptureDescription& d, LightSelection& lights) noexcept
{
    if (d.Width == 0 || d.Height == 0 || d.Width > 16384 || d.Height > 16384 || d.TargetSamples == 0 ||
        d.TargetSamples >= (uint32{1} << 24) || d.MaxDepth == 0 || d.MaxDepth > 64 || d.RouletteDepth == 0 ||
        d.RouletteDepth > 64 || !math::IsFinite(d.SecondaryDistance) || d.SecondaryDistance <= 0 ||
        d.SecondaryDistance > 2000000 || !internal::ReferenceColor(d.Environment, 10000) || d.TextureMip >= 16)
    {
        return Status::InvalidDescription;
    }
    return NormalizeDirectLights(d.Lights, d.LightCount, lights);
}
void Put32(uint8* output, uint32 value) noexcept
{
    static_cast<void>(foundation::TryWriteLittleEndian(value, {output, 4}));
}
void Put64(uint8* output, uint64 value) noexcept
{
    static_cast<void>(foundation::TryWriteLittleEndian(value, {output, 8}));
}
void PutFloat(uint8* output, float32 value) noexcept
{
    uint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Put32(output, bits);
}
bool ReferenceFingerprint(ReferenceJob& job, usize budget) noexcept
{
    // One bounded cold scratch stream, serialized without padding or host byte order.
    // Hashes are diagnostics; exact retained identities/parameters control resets.
    const usize triangleBytes = job.Scene.Triangles.GetSize() * 132;
    usize bytes = triangleBytes + job.Scene.InstanceCount * 116 + job.Description.LightCount * 64 + 12;
    if (bytes > budget)
    {
        return false;
    }
    Array<uint8> stream;
    if (!stream.TryResize(bytes))
    {
        return false;
    }
    usize offset = 0;
    const auto word = [&stream, &offset](uint32 value) noexcept {
        Put32(stream.GetData() + offset, value);
        offset += 4;
    };
    const auto scalar = [&stream, &offset](float32 value) noexcept {
        PutFloat(stream.GetData() + offset, value);
        offset += 4;
    };
    const auto vector = [&scalar](math::Vector3 value) noexcept {
        scalar(value.X);
        scalar(value.Y);
        scalar(value.Z);
    };
    for (const auto& triangle : job.Scene.Triangles)
    {
        for (auto p : triangle.Positions)
        {
            vector(p);
        }
        for (auto n : triangle.Normals)
        {
            vector(n);
        }
        for (auto uv : triangle.Uvs)
        {
            scalar(uv.X);
            scalar(uv.Y);
        }
        vector(triangle.GeometricNormal);
        vector(triangle.Error);
        scalar(triangle.Area);
        word(triangle.Instance);
        word(triangle.Primitive);
    }
    for (usize i = 0; i < job.Scene.InstanceCount; ++i)
    {
        const auto& instance = job.Scene.Instances[i];
        const auto& surface = *instance.Surface;
        Put64(stream.GetData() + offset, instance.SourceId);
        offset += 8;
        vector(surface.Bsdf.BaseColor);
        scalar(surface.Bsdf.Metallic);
        scalar(surface.Bsdf.Roughness);
        vector(surface.Emission);
        scalar(surface.Tint.W);
        scalar(surface.UvTransform.X);
        scalar(surface.UvTransform.Y);
        scalar(surface.UvTransform.Z);
        scalar(surface.UvTransform.W);
        scalar(surface.AlphaCutoff);
        word(static_cast<uint32>(surface.Alpha));
        word(static_cast<uint32>(surface.Sampler.Filter));
        word(static_cast<uint32>(surface.Sampler.U));
        word(static_cast<uint32>(surface.Sampler.V));
        word(static_cast<uint32>(instance.Texture.Format));
        word(instance.Texture.Width);
        word(instance.Texture.Height);
        Put64(stream.GetData() + offset, instance.Texture.RowPitch);
        offset += 8;
        const auto textureFingerprint =
            foundation::StableFingerprint128({instance.Texture.Bytes, instance.Texture.Size});
        Put64(stream.GetData() + offset, textureFingerprint.Low);
        offset += 8;
        Put64(stream.GetData() + offset, textureFingerprint.High);
        offset += 8;
    }
    for (usize i = 0; i < job.Description.LightCount; ++i)
    {
        const auto& light = job.Lights[i];
        Put64(stream.GetData() + offset, light.SourceId);
        offset += 8;
        word(static_cast<uint32>(light.Kind));
        vector(light.Color);
        scalar(light.Intensity);
        vector(light.Position);
        vector(light.Direction);
        scalar(light.Range);
        scalar(light.InnerCos);
        scalar(light.OuterCos);
    }
    vector(job.Description.Environment);
    const auto fingerprint = foundation::StableFingerprint128({stream.GetData(), offset});
    job.Scene.FingerprintLow = fingerprint.Low;
    job.Scene.FingerprintHigh = fingerprint.High;
    return true;
}
} // namespace
usize Renderer::State::RemainingReferenceRayBytes(Status& status) noexcept
{
    if (Reference->Options.Traversal == ReferenceTraversal::CpuOracle)
    {
        status = Status::Ready;
        return 0;
    }
    rhi::RayResourceStats stats;
    status = rhi::GetRayResourceStats(Device, stats);
    if (status != Status::Ready)
    {
        return 0;
    }
    const auto budget = Reference->Options.RayByteBudget;
    const usize query =
        usize(Reference->Options.TilePixels) * 2 * (sizeof(rhi::RayInput) + sizeof(rhi::RayResult)) + 16;
    if (stats.ResidentBytes > budget || stats.BuildScratchBytes > budget - stats.ResidentBytes ||
        query > budget - stats.ResidentBytes - stats.BuildScratchBytes)
    {
        status = Status::CapacityExceeded;
        return 0;
    }
    return budget - stats.ResidentBytes - stats.BuildScratchBytes - query;
}
Status Renderer::State::PrepareReferenceMesh(const MeshDescription& input,
                                             const struct Vertex* vertices,
                                             core::UniquePtr<internal::ReferenceMeshData>& output) noexcept
{
    for (usize i = 0; i < input.VertexCount; ++i)
    {
        const auto p = vertices[i].Position;
        const auto uv = vertices[i].Uv;
        if (math::Abs(p.X) > 1000000 || math::Abs(p.Y) > 1000000 || math::Abs(p.Z) > 1000000 ||
            math::Abs(uv.X) > 1000000 || math::Abs(uv.Y) > 1000000)
        {
            return Status::Unsupported;
        }
    }
    const auto bytesPerVertex = sizeof(math::Vector3) * 2 + sizeof(math::Vector2);
    const auto remaining = Reference->Options.CpuByteBudget - Reference->CpuBytes;
    if (input.VertexCount > remaining / bytesPerVertex)
    {
        return Status::CapacityExceeded;
    }
    const auto vertexBytes = input.VertexCount * bytesPerVertex;
    if (input.IndexCount > (remaining - vertexBytes) / sizeof(uint32) ||
        sizeof(internal::ReferenceMeshData) > remaining - vertexBytes - input.IndexCount * sizeof(uint32))
    {
        return Status::CapacityExceeded;
    }
    auto* allocation = new (std::nothrow) internal::ReferenceMeshData;
    if (allocation == nullptr)
    {
        return Status::OutOfMemory;
    }
    core::UniquePtr<internal::ReferenceMeshData> data(allocation);
    data->Device = Device;
    if (!data->Positions.TryResize(input.VertexCount) || !data->Normals.TryResize(input.VertexCount) ||
        !data->Uvs.TryResize(input.VertexCount) || !data->Indices.TryAddRange({input.Indices, input.IndexCount}))
    {
        return Status::OutOfMemory;
    }
    for (usize i = 0; i < input.VertexCount; ++i)
    {
        data->Positions[i] = vertices[i].Position;
        data->Normals[i] = vertices[i].Normal;
        data->Uvs[i] = vertices[i].Uv;
    }
    data->CpuBytes = sizeof(internal::ReferenceMeshData) + vertexBytes + input.IndexCount * sizeof(uint32);
    data->NativeStatus = Status::Ready;
    if (Reference->Options.Traversal == ReferenceTraversal::NativeQueries)
    {
        Status status = Status::Ready;
        const auto budget = RemainingReferenceRayBytes(status);
        if (status != Status::Ready)
        {
            return status;
        }
        const rhi::RayGeometryDescription description{data->Positions.GetData(),
                                                      input.VertexCount,
                                                      data->Indices.GetData(),
                                                      input.IndexCount,
                                                      data->Uvs.GetData(),
                                                      budget};
        data->NativeStatus = rhi::CreateRayGeometry(Device, description, data->Geometry);
        if (!Accepted(data->NativeStatus))
        {
            return data->NativeStatus;
        }
    }
    output = core::Move(data);
    return Status::Ready;
}
Status Renderer::State::PrepareReferenceSurface(const MaterialDescription& surface,
                                                const LitMaterialDescription* lighting,
                                                core::UniquePtr<internal::ReferenceSurfaceData>& output) noexcept
{
    if (lighting == nullptr)
    {
        return Status::Ready;
    }
    const auto uv = surface.UvTransform;
    if (math::Abs(uv.X) > 1000000 || math::Abs(uv.Y) > 1000000 || math::Abs(uv.Z) > 1000000 ||
        math::Abs(uv.W) > 1000000)
    {
        return Status::Unsupported;
    }
    if (sizeof(internal::ReferenceSurfaceData) > Reference->Options.CpuByteBudget - Reference->CpuBytes)
    {
        return Status::CapacityExceeded;
    }
    auto* data = new (std::nothrow) internal::ReferenceSurfaceData;
    if (data == nullptr)
    {
        return Status::OutOfMemory;
    }
    data->Bsdf = {{surface.Tint.X, surface.Tint.Y, surface.Tint.Z}, lighting->Metallic, lighting->Roughness};
    data->Tint = surface.Tint;
    data->UvTransform = surface.UvTransform;
    data->Sampler = surface.Sampler;
    data->Alpha = surface.Alpha;
    data->AlphaCutoff = surface.AlphaCutoff;
    data->Emission = lighting->Emission;
    data->HasTexture = !rhi::IsNull(surface.Texture);
    output.Reset(data);
    return Status::Ready;
}
void Renderer::State::DropReferenceCapture(usize index) noexcept
{
    auto& record = Reference->Captures[index];
    if (!record.Used)
    {
        return;
    }
    const auto snapshot = record.SnapshotSlot;
    Reference->CpuBytes -= record.Job->Info.CpuBytes;
    record.Job.Reset();
    record.Used = record.Owned = false;
    ++record.Generation;
    --SnapshotRecords[snapshot].References;
    DropSnapshot(snapshot);
}
void Renderer::State::ClearReference() noexcept
{
    if (!Reference)
    {
        return;
    }
    for (usize i = 0; i < 4; ++i)
    {
        DropReferenceCapture(i);
    }
    for (auto& mesh : MeshRecords)
    {
        mesh.Reference.Reset();
    }
    for (auto& material : Materials)
    {
        material.Reference.Reset();
    }
    static_cast<void>(rhi::Destroy(Device, Reference->Program));
    Reference.Reset();
}
Status Renderer::EnableReferenceRendering(const rhi::ShaderDescription& shader,
                                          const ReferenceRenderingOptions& options) noexcept
{
    if (!mState || mState->Setup != Status::Ready)
    {
        return Status::NotReady;
    }
    if (mState->Reference)
    {
        return Status::InvalidState;
    }
    for (const auto& mesh : mState->MeshRecords)
    {
        if (mesh.Used)
        {
            return Status::InvalidState;
        }
    }
    for (const auto& material : mState->Materials)
    {
        if (material.Used)
        {
            return Status::InvalidState;
        }
    }
    if (static_cast<uint8>(options.Traversal) > 1 || options.TilePixels == 0 || options.TilePixels > 2048 ||
        options.CpuByteBudget < sizeof(internal::ReferenceState))
    {
        return Status::InvalidDescription;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    if (options.Traversal == ReferenceTraversal::NativeQueries)
    {
        rhi::RayCapabilities caps;
        const auto status = rhi::GetRayCapabilities(mState->Device, caps);
        if (status != Status::Ready)
        {
            return status;
        }
        if (options.RayByteBudget == 0 || usize(options.TilePixels) * 2 > caps.MaxRays)
        {
            return Status::InvalidDescription;
        }
    }
    auto* allocation = new (std::nothrow) internal::ReferenceState;
    if (allocation == nullptr)
    {
        return Status::OutOfMemory;
    }
    core::UniquePtr<internal::ReferenceState> state(allocation);
    state->Options = options;
    state->CpuBytes = sizeof(internal::ReferenceState);
    if (options.Traversal == ReferenceTraversal::NativeQueries)
    {
        const auto status = rhi::CreateRayQueryProgram(mState->Device, shader, state->Program);
        if (status != Status::Ready)
        {
            return status;
        }
    }
    mState->Reference = core::Move(state);
    return Status::Ready;
}
Status Renderer::DisableReferenceRendering() noexcept
{
    if (!mState)
    {
        return Status::InvalidState;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    mState->ClearReference();
    return Status::Ready;
}
} // namespace ludus::graphics::renderer

namespace ludus::graphics::renderer
{
Status Renderer::State::BuildReferenceJob(usize snapshotSlot,
                                          const ReferenceCaptureDescription& description,
                                          core::UniquePtr<internal::ReferenceJob>& output) noexcept
{
    LightSelection lights;
    auto status = ValidateReferenceDescription(description, lights);
    if (status != Status::Ready)
    {
        return status;
    }
    const auto& snapshot = SnapshotRecords[snapshotSlot];
    usize triangles = 0, instances = 0, emitters = 0;
    uint64 alphaScalars = 0;
    for (usize i = 0; i < snapshot.Count; ++i)
    {
        const auto& item = snapshot.Items[i];
        if (item.Input.Pass == Layer::Overlay)
        {
            continue;
        }
        if (item.MaterialSlot == MATERIALS || !Materials[item.MaterialSlot].Lit ||
            !Materials[item.MaterialSlot].Reference || !MeshRecords[item.MeshSlot].Reference)
        {
            return Status::Unsupported;
        }
        const auto& transform = item.Input.Transform;
        for (auto column : transform.Columns)
        {
            if (math::Abs(column.X) > 1000000 || math::Abs(column.Y) > 1000000 || math::Abs(column.Z) > 1000000)
            {
                return Status::Unsupported;
            }
        }
        if (math::Abs(transform.Translation.X) > 1000000 || math::Abs(transform.Translation.Y) > 1000000 ||
            math::Abs(transform.Translation.Z) > 1000000)
        {
            return Status::Unsupported;
        }
        const auto& mesh = *MeshRecords[item.MeshSlot].Reference;
        const auto& material = Materials[item.MaterialSlot];
        const auto count = mesh.Indices.GetSize() / 3;
        if (count > (~uint32{0}) / 2 - triangles)
        {
            return Status::CapacityExceeded;
        }
        triangles += count;
        const auto& emission = material.Reference->Emission;
        if (emission.X > 0 || emission.Y > 0 || emission.Z > 0)
        {
            emitters += count;
        }
        if (material.Reference->HasTexture)
        {
            rhi::TextureCpuContent texture;
            status = rhi::GetTextureCpuContent(Device, material.Texture, description.TextureMip, texture);
            if (status != Status::Ready)
            {
                return status;
            }
            if (material.Alpha == MaterialAlpha::Masked)
            {
                alphaScalars += uint64(texture.Width) * texture.Height;
            }
        }
        ++instances;
    }
    const usize pixels = usize(description.Width) * description.Height, tile = Reference->Options.TilePixels;
    const bool oracle = Reference->Options.Traversal == ReferenceTraversal::CpuOracle;
    const uint64 bvhBytes = oracle ? uint64(triangles) * (2 * sizeof(internal::ReferenceBvhNode) + sizeof(uint32)) : 0;
    const uint64 fixedWide = sizeof(ReferenceJob) + uint64(pixels) * sizeof(internal::ReferenceAccumulator) +
                             uint64(tile) * sizeof(internal::ReferencePath) +
                             uint64(tile) * 2 * (sizeof(rhi::RayInput) + sizeof(rhi::RayResult)) +
                             uint64(triangles) * sizeof(ReferenceTriangle) + bvhBytes +
                             uint64(emitters) * sizeof(uint32);
    const uint64 signatureBytes = uint64(triangles) * 132 + uint64(instances) * 116 + uint64(lights.Count) * 64 + 12;
    const uint64 nativeSetup =
        oracle ? 0
               : uint64(instances) * (sizeof(rhi::RayInstanceDescription) + sizeof(Array<float32>)) +
                     alphaScalars * sizeof(float32);
    const uint64 transientWide =
        math::Max(signatureBytes, math::Max(nativeSetup, oracle ? uint64(triangles) * sizeof(uint32) : uint64{0}));
    usize fixed = 0, transient = 0;
    // Bounded source counts fit uint64; convert only at the wasm32 allocation boundary.
    if (!foundation::TryIntegerCast(fixedWide, fixed) || !foundation::TryIntegerCast(transientWide, transient))
    {
        return Status::CapacityExceeded;
    }
    const auto remaining = Reference->Options.CpuByteBudget - Reference->CpuBytes;
    if (fixed > remaining || transient > remaining - fixed)
    {
        return Status::CapacityExceeded;
    }
    auto* allocation = new (std::nothrow) ReferenceJob;
    if (allocation == nullptr)
    {
        return Status::OutOfMemory;
    }
    core::UniquePtr<ReferenceJob> job(allocation);
    job->Device = Device;
    job->Description = description;
    job->Description.Lights = job->Lights;
    job->Description.LightCount = lights.Count;
    for (usize i = 0; i < lights.Count; ++i)
    {
        job->Lights[i] = lights.Lights[i];
    }
    if (math::TryMakeRandomKey(description.Seed, 0x52543031, job->Key) != math::MathStatus::Success)
    {
        return Status::InvalidDescription;
    }
    if (!job->Pixels.TryEnsureCapacity(pixels) || !job->Pixels.TryResize(pixels) ||
        !job->Paths.TryEnsureCapacity(tile) || !job->Paths.TryResize(tile) || !job->Rays.TryEnsureCapacity(tile * 2) ||
        !job->Rays.TryResize(tile * 2) || !job->Hits.TryEnsureCapacity(tile * 2) || !job->Hits.TryResize(tile * 2) ||
        !job->Scene.Triangles.TryEnsureCapacity(triangles) || !job->Scene.Triangles.TryResize(triangles) ||
        !job->Scene.Emitters.TryEnsureCapacity(emitters))
    {
        return Status::OutOfMemory;
    }
    job->Scene.Bounds = math::EmptyAabb3();
    usize triangleAt = 0;
    for (usize i = 0; i < snapshot.Count; ++i)
    {
        const auto& item = snapshot.Items[i];
        if (item.Input.Pass == Layer::Overlay)
        {
            continue;
        }
        const auto& mesh = *MeshRecords[item.MeshSlot].Reference;
        const auto& material = Materials[item.MaterialSlot];
        auto& instance = job->Scene.Instances[job->Scene.InstanceCount];
        instance.Surface = material.Reference.Get();
        instance.SourceId = item.Input.SourceId;
        instance.FirstTriangle = triangleAt;
        instance.TriangleCount = mesh.Indices.GetSize() / 3;
        if (instance.Surface->HasTexture)
        {
            status = rhi::GetTextureCpuContent(Device, material.Texture, description.TextureMip, instance.Texture);
            if (status != Status::Ready)
            {
                return status;
            }
        }
        math::Matrix3 normalMatrix;
        if (math::TryNormalMatrix(item.Input.Transform, {}, normalMatrix) != math::MathStatus::Success)
        {
            return Status::InvalidDescription;
        }
        for (usize primitive = 0; primitive < instance.TriangleCount; ++primitive)
        {
            auto& triangle = job->Scene.Triangles[triangleAt];
            triangle.Instance = static_cast<uint32>(job->Scene.InstanceCount);
            triangle.Primitive = static_cast<uint32>(primitive);
            for (usize corner = 0; corner < 3; ++corner)
            {
                const auto vertex = mesh.Indices[primitive * 3 + corner];
                triangle.Positions[corner] = math::TransformPoint(item.Input.Transform, mesh.Positions[vertex]);
                triangle.Normals[corner] = internal::ReferenceNormalize(normalMatrix * mesh.Normals[vertex]);
                triangle.Uvs[corner] = mesh.Uvs[vertex];
                const auto p = triangle.Positions[corner];
                if (!math::IsFinite(p) || math::Abs(p.X) > 1000000 || math::Abs(p.Y) > 1000000 ||
                    math::Abs(p.Z) > 1000000)
                {
                    return Status::Unsupported;
                }
                job->Scene.Bounds = math::Merge(job->Scene.Bounds, {p, p});
                // Conservative FP32 affine/barycentric arithmetic allowance, using
                // absolute products before cancellation, not a scene-sized epsilon.
                const auto local = mesh.Positions[vertex];
                const auto& a = item.Input.Transform;
                const math::Vector3 error{
                    (math::Abs(a.Columns[0].X * local.X) + math::Abs(a.Columns[1].X * local.Y) +
                     math::Abs(a.Columns[2].X * local.Z) + math::Abs(a.Translation.X)) *
                        1e-6F,
                    (math::Abs(a.Columns[0].Y * local.X) + math::Abs(a.Columns[1].Y * local.Y) +
                     math::Abs(a.Columns[2].Y * local.Z) + math::Abs(a.Translation.Y)) *
                        1e-6F,
                    (math::Abs(a.Columns[0].Z * local.X) + math::Abs(a.Columns[1].Z * local.Y) +
                     math::Abs(a.Columns[2].Z * local.Z) + math::Abs(a.Translation.Z)) *
                        1e-6F,
                };
                triangle.Error = {math::Max(triangle.Error.X, error.X),
                                  math::Max(triangle.Error.Y, error.Y),
                                  math::Max(triangle.Error.Z, error.Z)};
            }
            const auto areaVector = math::Cross(triangle.Positions[1] - triangle.Positions[0],
                                                triangle.Positions[2] - triangle.Positions[0]);
            triangle.Area = math::Length(areaVector) * 0.5F;
            const auto i0 = mesh.Indices[primitive * 3], i1 = mesh.Indices[primitive * 3 + 1],
                       i2 = mesh.Indices[primitive * 3 + 2];
            const auto localNormal =
                math::Cross(mesh.Positions[i1] - mesh.Positions[i0], mesh.Positions[i2] - mesh.Positions[i0]);
            triangle.GeometricNormal = internal::ReferenceNormalize(normalMatrix * localNormal);
            if (!math::IsFinite(triangle.Area) || triangle.Area <= 0 || math::Length(triangle.GeometricNormal) == 0)
            {
                return Status::Unsupported;
            }
            for (auto normal : triangle.Normals)
            {
                if (!math::IsFinite(normal) || math::Dot(normal, triangle.GeometricNormal) <= 0)
                {
                    return Status::Unsupported;
                }
            }
            const auto& emission = instance.Surface->Emission;
            if ((emission.X > 0 || emission.Y > 0 || emission.Z > 0) &&
                !job->Scene.Emitters.TryAdd(static_cast<uint32>(triangleAt)))
            {
                return Status::OutOfMemory;
            }
            ++triangleAt;
        }
        ++job->Scene.InstanceCount;
    }
    if (oracle)
    {
        status = internal::BuildReferenceBvh(job->Scene);
        if (status != Status::Ready)
        {
            return status;
        }
    }
    status = internal::PrepareReferenceCamera(*job);
    if (status != Status::Ready)
    {
        return status;
    }
    if (!ReferenceFingerprint(*job, remaining - fixed))
    {
        return Status::OutOfMemory;
    }
    job->Info.Traversal = Reference->Options.Traversal;
    job->Info.Seed = description.Seed;
    job->Info.Width = description.Width;
    job->Info.Height = description.Height;
    job->Info.TargetSamples = description.TargetSamples;
    job->Info.MaxDepth = description.MaxDepth;
    job->Info.SceneFingerprintLow = job->Scene.FingerprintLow;
    job->Info.SceneFingerprintHigh = job->Scene.FingerprintHigh;
    job->Info.CpuBytes =
        sizeof(ReferenceJob) + job->Pixels.GetCapacity() * sizeof(internal::ReferenceAccumulator) +
        job->Paths.GetCapacity() * sizeof(internal::ReferencePath) + job->Rays.GetCapacity() * sizeof(rhi::RayInput) +
        job->Hits.GetCapacity() * sizeof(rhi::RayResult) +
        job->Scene.Triangles.GetCapacity() * sizeof(ReferenceTriangle) +
        job->Scene.Order.GetCapacity() * sizeof(uint32) + job->Scene.Emitters.GetCapacity() * sizeof(uint32) +
        job->Scene.Nodes.GetCapacity() * sizeof(internal::ReferenceBvhNode);
    if (job->Info.CpuBytes > remaining || transient > remaining - job->Info.CpuBytes)
    {
        return Status::CapacityExceeded;
    }
    job->Stage = ReferenceStage::Primary;
    if (!oracle && instances != 0)
    {
        Array<rhi::RayInstanceDescription> native;
        Array<Array<float32>> alpha;
        if (!native.TryResize(instances) || !alpha.TryResize(instances))
        {
            return Status::OutOfMemory;
        }
        usize at = 0;
        for (usize i = 0; i < snapshot.Count; ++i)
        {
            const auto& item = snapshot.Items[i];
            if (item.Input.Pass == Layer::Overlay)
            {
                continue;
            }
            const auto& mesh = *MeshRecords[item.MeshSlot].Reference;
            const auto geometry = rhi::GetStatus(Device, mesh.Geometry);
            if (geometry != Status::Ready)
            {
                return geometry == Status::Pending ? Status::NotReady : geometry;
            }
            auto& target = native[at];
            const auto& source = job->Scene.Instances[at];
            const auto& surface = *source.Surface;
            target.Geometry = mesh.Geometry;
            const auto& transform = item.Input.Transform;
            target.Transform[0] = transform.Columns[0].X;
            target.Transform[1] = transform.Columns[1].X;
            target.Transform[2] = transform.Columns[2].X;
            target.Transform[3] = transform.Translation.X;
            target.Transform[4] = transform.Columns[0].Y;
            target.Transform[5] = transform.Columns[1].Y;
            target.Transform[6] = transform.Columns[2].Y;
            target.Transform[7] = transform.Translation.Y;
            target.Transform[8] = transform.Columns[0].Z;
            target.Transform[9] = transform.Columns[1].Z;
            target.Transform[10] = transform.Columns[2].Z;
            target.Transform[11] = transform.Translation.Z;
            target.AlphaMasked = surface.Alpha == MaterialAlpha::Masked;
            target.Opacity = surface.Tint.W;
            target.AlphaCutoff = surface.AlphaCutoff;
            target.UvTransform[0] = surface.UvTransform.X;
            target.UvTransform[1] = surface.UvTransform.Y;
            target.UvTransform[2] = surface.UvTransform.Z;
            target.UvTransform[3] = surface.UvTransform.W;
            target.LinearFilter = surface.Sampler.Filter == rhi::RasterFilter::Linear;
            target.RepeatU = surface.Sampler.U == rhi::RasterAddress::Repeat;
            target.RepeatV = surface.Sampler.V == rhi::RasterAddress::Repeat;
            if (target.AlphaMasked && surface.HasTexture)
            {
                const auto& image = source.Texture;
                const auto size = usize(image.Width) * image.Height;
                if (!alpha[at].TryResize(size))
                {
                    return Status::OutOfMemory;
                }
                for (usize y = 0; y < image.Height; ++y)
                {
                    for (usize x = 0; x < image.Width; ++x)
                    {
                        alpha[at][y * image.Width + x] = float32(image.Bytes[y * image.RowPitch + x * 4 + 3]) / 255;
                    }
                }
                target.Alpha = alpha[at].GetData();
                target.Width = image.Width;
                target.Height = image.Height;
            }
            ++at;
        }
        const auto budget = RemainingReferenceRayBytes(status);
        if (status != Status::Ready)
        {
            return status;
        }
        if (budget == 0)
        {
            return Status::CapacityExceeded;
        }
        status = rhi::CreateRayScene(Device, {native.GetData(), instances, budget}, job->Scene.Native);
        if (!Accepted(status))
        {
            return status;
        }
        job->Stage = status == Status::Pending ? ReferenceStage::Setup : ReferenceStage::Primary;
    }
    const auto result = job->Stage == ReferenceStage::Setup ? Status::Pending : Status::Ready;
    output = core::Move(job);
    return result;
}
Status Renderer::BeginReferenceCapture(Snapshot snapshot,
                                       const ReferenceCaptureDescription& description,
                                       ReferenceCapture& output) noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::NotReady;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    if (Access::Owner(output) != 0)
    {
        return Status::InvalidState;
    }
    auto* source = Resolve(mState->SnapshotRecords, mState->Owner, snapshot);
    if (source == nullptr)
    {
        return Status::InvalidHandle;
    }
    auto& state = *mState->Reference;
    usize slot = 0;
    auto status = FreeSlot(state.Captures, slot);
    if (status != Status::Ready)
    {
        return status;
    }
    core::UniquePtr<ReferenceJob> job;
    status = mState->BuildReferenceJob(Access::Slot(snapshot), description, job);
    if (!Accepted(status))
    {
        return status;
    }
    auto& record = state.Captures[slot];
    record.SnapshotSlot = Access::Slot(snapshot);
    state.CpuBytes += job->Info.CpuBytes;
    record.Job = core::Move(job);
    record.Used = record.Owned = true;
    ++source->References;
    output = Access::Make<ReferenceCapture>({mState->Owner, record.Generation, slot});
    return status;
}
Status Renderer::Release(ReferenceCapture& handle) noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    mState->DropReferenceCapture(Access::Slot(handle));
    handle = {};
    return Status::Ready;
}
Status Renderer::GetStatus(ReferenceCapture handle) const noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto admission = ReferenceAdmission(mState->Device, false);
    if (admission != Status::Ready)
    {
        return admission;
    }
    const auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    if (record->Job->Stage == ReferenceStage::Failed)
    {
        return Status::Failed;
    }
    return record->Job->Stage == ReferenceStage::Complete ? Status::Ready : Status::Pending;
}
Status Renderer::GetReferenceCaptureInfo(ReferenceCapture handle, ReferenceCaptureInfo& output) const noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    output = record->Job->Info;
    return Status::Ready;
}
Status Renderer::CopyReferencePixels(ReferenceCapture handle, ReferencePixel* output, usize count) const noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    const auto& pixels = record->Job->Pixels;
    if (output == nullptr || count != pixels.GetSize())
    {
        return Status::InvalidDescription;
    }
    for (usize i = 0; i < count; ++i)
    {
        const auto& p = pixels[i];
        const auto weight = p.Samples == 0 ? 0.0F : 1 / float32(p.Samples);
        const auto emission = p.Emission.Value * weight, direct = p.Direct.Value * weight,
                   indirect = p.Indirect.Value * weight;
        output[i] = {emission + direct + indirect, emission, direct, indirect, p.Samples};
    }
    return Status::Ready;
}
} // namespace ludus::graphics::renderer

namespace ludus::graphics::renderer
{
namespace
{
Status
ReferenceQuery(ReferenceJob& job, rhi::RayQueryProgram program, ReferenceTraversal traversal, usize rayBudget) noexcept
{
    uint64 closest = 0, visibility = 0;
    for (usize i = 0; i < job.QueryCount; ++i)
    {
        const auto origin = internal::ReferenceOrigin(job.Rays[i]);
        if (!math::IsFinite(origin) || math::Abs(origin.X) > 1000000 || math::Abs(origin.Y) > 1000000 ||
            math::Abs(origin.Z) > 1000000)
        {
            ++job.Info.InvalidContributions;
            for (usize path = 0; path < job.PathCount; ++path)
            {
                const auto& candidate = job.Paths[path];
                if ((job.Stage == ReferenceStage::Primary && path == i) || candidate.ShadowQuery == i ||
                    candidate.NextQuery == i)
                {
                    job.Info.FailurePixel = candidate.Pixel;
                    job.Info.FailureDepth = candidate.Depth;
                    break;
                }
            }
            return Status::Failed;
        }
        if (job.Rays[i].VisibilityOnly != 0)
        {
            ++visibility;
        }
        else
        {
            ++closest;
        }
    }
    if (traversal == ReferenceTraversal::CpuOracle || job.Scene.InstanceCount == 0)
    {
        for (usize i = 0; i < job.QueryCount; ++i)
        {
            const auto status = internal::TraceReferenceOracle(job.Scene, job.Rays[i], job.Hits[i]);
            if (status != Status::Ready)
            {
                return status;
            }
        }
        job.Info.ClosestQueries += closest;
        job.Info.VisibilityQueries += visibility;
        return Status::Ready;
    }
    rhi::RayResourceStats stats;
    const auto observed = rhi::GetRayResourceStats(job.Device, stats);
    if (observed != Status::Ready)
    {
        return observed;
    }
    const usize needed = job.QueryCount * (sizeof(rhi::RayInput) + sizeof(rhi::RayResult)) + 16;
    if (stats.ResidentBytes > rayBudget || stats.BuildScratchBytes > rayBudget - stats.ResidentBytes ||
        needed > rayBudget - stats.ResidentBytes - stats.BuildScratchBytes)
    {
        return Status::CapacityExceeded;
    }
    const auto status =
        rhi::SubmitRayQueries(job.Device, program, job.Scene.Native, job.Rays.GetData(), job.QueryCount, job.Batch);
    if (Accepted(status))
    {
        job.Info.ClosestQueries += closest;
        job.Info.VisibilityQueries += visibility;
    }
    return status;
}
Status ReferenceRead(ReferenceJob& job, ReferenceTraversal traversal) noexcept
{
    if (traversal == ReferenceTraversal::CpuOracle || job.Scene.InstanceCount == 0)
    {
        return Status::Ready;
    }
    const auto status = rhi::GetStatus(job.Device, job.Batch);
    if (status != Status::Ready)
    {
        return status;
    }
    const auto read = rhi::ReadRayResults(job.Device, job.Batch, job.Hits.GetData(), job.QueryCount);
    if (read != Status::Ready)
    {
        return read;
    }
    return rhi::Destroy(job.Device, job.Batch);
}
bool FiniteReferenceSum(const internal::ReferenceSum& sum) noexcept
{
    return math::IsFinite(sum.Value) && math::IsFinite(sum.Compensation) && sum.Value.X >= 0 && sum.Value.Y >= 0 &&
           sum.Value.Z >= 0;
}
} // namespace
Status Renderer::AdvanceReferenceCapture(ReferenceCapture handle) noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    auto& state = *mState->Reference;
    auto* record = Resolve(state.Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    auto& job = *record->Job;
    if (job.Stage == ReferenceStage::Failed)
    {
        return Status::Failed;
    }
    if (job.Stage == ReferenceStage::Complete)
    {
        return Status::Ready;
    }
    const auto fail = [&job](Status status) noexcept {
        job.Stage = ReferenceStage::Failed;
        return status;
    };
    if (job.Stage == ReferenceStage::Setup)
    {
        const auto status = rhi::GetStatus(job.Device, job.Scene.Native);
        if (status == Status::Pending)
        {
            return status;
        }
        if (status != Status::Ready)
        {
            return fail(status);
        }
        job.Stage = ReferenceStage::Primary;
        return Status::Pending;
    }
    if (job.Stage == ReferenceStage::Primary)
    {
        job.PathCount = math::Min(usize(state.Options.TilePixels), job.Pixels.GetSize() - job.FirstPixel);
        job.QueryCount = job.PathCount;
        for (usize i = 0; i < job.PathCount; ++i)
        {
            auto& path = job.Paths[i];
            path = {};
            path.Pixel = static_cast<uint32>(job.FirstPixel + i);
            path.Sample = job.Pixels[path.Pixel].Samples;
            const auto status = internal::GenerateReferencePrimary(job, path, path.Ray);
            if (status != Status::Ready)
            {
                return fail(status);
            }
            job.Rays[i] = path.Ray;
        }
        const auto status = ReferenceQuery(job, state.Program, state.Options.Traversal, state.Options.RayByteBudget);
        if (!Accepted(status))
        {
            return fail(status);
        }
        job.Stage = ReferenceStage::WaitPrimary;
        return Status::Pending;
    }
    if (job.Stage == ReferenceStage::WaitPrimary)
    {
        const auto status = ReferenceRead(job, state.Options.Traversal);
        if (status == Status::Pending)
        {
            return status;
        }
        if (status != Status::Ready)
        {
            return fail(status);
        }
        for (usize i = 0; i < job.PathCount; ++i)
        {
            const auto prepared = internal::PrepareReferenceVertex(job, job.Paths[i], job.Hits[i]);
            if (prepared != Status::Ready)
            {
                return fail(prepared);
            }
        }
        job.Stage = ReferenceStage::Bounce;
        return Status::Pending;
    }
    if (job.Stage == ReferenceStage::Bounce)
    {
        job.QueryCount = 0;
        for (usize i = 0; i < job.PathCount; ++i)
        {
            auto& path = job.Paths[i];
            path.ShadowQuery = path.NextQuery = ~usize{0};
            if (path.HasShadow)
            {
                path.ShadowQuery = job.QueryCount;
                job.Rays[job.QueryCount++] = path.Shadow;
            }
            if (path.HasNext)
            {
                path.NextQuery = job.QueryCount;
                job.Rays[job.QueryCount++] = path.Next;
            }
        }
        if (job.QueryCount == 0)
        {
            job.Stage = ReferenceStage::Commit;
            return Status::Pending;
        }
        const auto status = ReferenceQuery(job, state.Program, state.Options.Traversal, state.Options.RayByteBudget);
        if (!Accepted(status))
        {
            return fail(status);
        }
        job.Stage = ReferenceStage::WaitBounce;
        return Status::Pending;
    }
    if (job.Stage == ReferenceStage::WaitBounce)
    {
        const auto status = ReferenceRead(job, state.Options.Traversal);
        if (status == Status::Pending)
        {
            return status;
        }
        if (status != Status::Ready)
        {
            return fail(status);
        }
        for (usize i = 0; i < job.PathCount; ++i)
        {
            auto& path = job.Paths[i];
            if (path.ShadowQuery != ~usize{0})
            {
                const auto hit = job.Hits[path.ShadowQuery].Hit;
                if (hit > 1)
                {
                    return fail(Status::Failed);
                }
                if (hit == 0)
                {
                    if (path.DirectDepth == 0)
                    {
                        path.Direct = path.Direct + path.PendingDirect;
                    }
                    else
                    {
                        path.Indirect = path.Indirect + path.PendingDirect;
                    }
                }
            }
            path.HasShadow = false;
            if (path.NextQuery != ~usize{0})
            {
                path.Ray = path.Next;
                const auto prepared = internal::PrepareReferenceVertex(job, path, job.Hits[path.NextQuery]);
                if (prepared != Status::Ready)
                {
                    return fail(prepared);
                }
            }
            else
            {
                path.HasNext = false;
            }
        }
        job.Stage = ReferenceStage::Bounce;
        return Status::Pending;
    }
    if (job.Stage == ReferenceStage::Commit)
    {
        // Check every candidate sum before publishing any pixel in this tile.
        for (usize i = 0; i < job.PathCount; ++i)
        {
            const auto& path = job.Paths[i];
            auto trial = job.Pixels[path.Pixel];
            trial.Emission.Add(path.Emission);
            trial.Direct.Add(path.Direct);
            trial.Indirect.Add(path.Indirect);
            if (!FiniteReferenceSum(trial.Emission) || !FiniteReferenceSum(trial.Direct) ||
                !FiniteReferenceSum(trial.Indirect) ||
                !math::IsFinite(trial.Emission.Value + trial.Direct.Value + trial.Indirect.Value))
            {
                ++job.Info.InvalidContributions;
                job.Info.FailurePixel = path.Pixel;
                job.Info.FailureDepth = path.Depth;
                return fail(Status::Failed);
            }
            if (trial.Samples >= (uint32{1} << 24) - 1)
            {
                return fail(Status::CapacityExceeded);
            }
        }
        for (usize i = 0; i < job.PathCount; ++i)
        {
            const auto& path = job.Paths[i];
            auto& pixel = job.Pixels[path.Pixel];
            pixel.Emission.Add(path.Emission);
            pixel.Direct.Add(path.Direct);
            pixel.Indirect.Add(path.Indirect);
            ++pixel.Samples;
            ++job.Info.CommittedSamples;
        }
        job.FirstPixel += job.PathCount;
        job.Info.MaximumSamples = job.Info.MinimumSamples + 1;
        if (job.FirstPixel == job.Pixels.GetSize())
        {
            job.FirstPixel = 0;
            ++job.Info.MinimumSamples;
            job.Info.MaximumSamples = job.Info.MinimumSamples;
        }
        job.Info.Complete = job.Info.MinimumSamples >= job.Description.TargetSamples;
        job.Stage = job.Info.Complete ? ReferenceStage::Complete : ReferenceStage::Primary;
        return job.Info.Complete ? Status::Ready : Status::Pending;
    }
    return fail(Status::InvalidState);
}
Status Renderer::UpdateReferenceCapture(ReferenceCapture handle,
                                        Snapshot snapshot,
                                        const ReferenceCaptureDescription& description) noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto admission = ReferenceAdmission(mState->Device);
    if (admission != Status::Ready)
    {
        return admission;
    }
    auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    auto* source = Resolve(mState->SnapshotRecords, mState->Owner, snapshot);
    if (record == nullptr || source == nullptr)
    {
        return Status::InvalidHandle;
    }
    LightSelection lights;
    auto status = ValidateReferenceDescription(description, lights);
    if (status != Status::Ready)
    {
        return status;
    }
    auto& old = *record->Job;
    if (record->SnapshotSlot == Access::Slot(snapshot) && SameReferenceIntegral(old, description, lights))
    {
        old.Description.TargetSamples = description.TargetSamples;
        old.Info.TargetSamples = description.TargetSamples;
        old.Info.Complete = old.Info.MinimumSamples >= description.TargetSamples;
        if (old.Info.Complete)
        {
            static_cast<void>(rhi::Destroy(old.Device, old.Batch));
            old.Stage = ReferenceStage::Complete;
        }
        else if (old.Stage == ReferenceStage::Complete || old.Stage == ReferenceStage::Failed)
        {
            static_cast<void>(rhi::Destroy(old.Device, old.Batch));
            old.Stage = ReferenceStage::Primary;
        }
        return old.Info.Complete ? Status::Ready : Status::Pending;
    }
    if (old.Info.Revision == LAST_ID)
    {
        return Status::IdentityExhausted;
    }
    core::UniquePtr<ReferenceJob> replacement;
    status = mState->BuildReferenceJob(Access::Slot(snapshot), description, replacement);
    if (!Accepted(status))
    {
        return status;
    }
    replacement->Info.Revision = old.Info.Revision + 1;
    ++source->References;
    const auto oldSnapshot = record->SnapshotSlot;
    auto& state = *mState->Reference;
    state.CpuBytes -= old.Info.CpuBytes;
    record->Job.Reset();
    state.CpuBytes += replacement->Info.CpuBytes;
    record->Job = core::Move(replacement);
    record->SnapshotSlot = Access::Slot(snapshot);
    --mState->SnapshotRecords[oldSnapshot].References;
    mState->DropSnapshot(oldSnapshot);
    return status;
}
Status
Renderer::EncodeReferenceHdr(ReferenceCapture handle, uint8* output, usize capacity, usize& written) const noexcept
{
    if (!mState || !mState->Reference)
    {
        return Status::InvalidHandle;
    }
    const auto* record = Resolve(mState->Reference->Captures, mState->Owner, handle);
    if (record == nullptr)
    {
        return Status::InvalidHandle;
    }
    const auto& job = *record->Job;
    constexpr usize headerBytes = 256, pixelBytes = 16;
    if (job.Pixels.GetSize() > (~usize{0} - headerBytes) / pixelBytes)
    {
        return Status::CapacityExceeded;
    }
    const auto size = headerBytes + job.Pixels.GetSize() * pixelBytes;
    if (output == nullptr)
    {
        return Status::InvalidDescription;
    }
    if (capacity < size)
    {
        return Status::CapacityExceeded;
    }
    std::memset(output, 0, headerBytes);
    const uint8 magic[8]{'L', 'U', 'H', 'D', 'R', '0', '1', 0};
    std::memcpy(output, magic, sizeof(magic));
    Put32(output + 8, 1);
    Put32(output + 12, job.Info.ProfileVersion);
    Put32(output + 16, job.Info.SamplerVersion);
    Put32(output + 20, static_cast<uint32>(headerBytes));
    Put32(output + 24, job.Description.Width);
    Put32(output + 28, job.Description.Height);
    Put32(output + 32, job.Description.TargetSamples);
    Put32(output + 36, job.Description.MaxDepth);
    Put32(output + 40, job.Description.RouletteDepth);
    const uint32 flags = (job.Info.Traversal == ReferenceTraversal::CpuOracle ? 1U : 0U) |
                         (job.Description.CenterRays ? 2U : 0U) | (job.Description.OrthographicCamera ? 4U : 0U) |
                         (job.Description.ReverseZ ? 8U : 0U) | (job.Description.InfiniteFar ? 16U : 0U) |
                         (!job.Info.Complete ? 32U : 0U);
    Put32(output + 44, flags);
    Put64(output + 48, job.Description.Seed);
    Put64(output + 56, job.Info.Revision);
    Put64(output + 64, job.Scene.FingerprintLow);
    Put64(output + 72, job.Scene.FingerprintHigh);
    PutFloat(output + 80, job.Description.SecondaryDistance);
    Put32(output + 84, job.Description.TextureMip);
    Put32(output + 88, static_cast<uint32>(job.Description.LightCount));
    usize at = 96;
    for (const auto& column : job.Description.WorldToClip.Columns)
    {
        PutFloat(output + at, column.X);
        PutFloat(output + at + 4, column.Y);
        PutFloat(output + at + 8, column.Z);
        PutFloat(output + at + 12, column.W);
        at += 16;
    }
    PutFloat(output + 160, job.Description.CameraPosition.X);
    PutFloat(output + 164, job.Description.CameraPosition.Y);
    PutFloat(output + 168, job.Description.CameraPosition.Z);
    PutFloat(output + 172, job.Description.Environment.X);
    PutFloat(output + 176, job.Description.Environment.Y);
    PutFloat(output + 180, job.Description.Environment.Z);
    for (usize i = 0; i < job.Pixels.GetSize(); ++i)
    {
        const auto& pixel = job.Pixels[i];
        const auto scale = pixel.Samples == 0 ? 0.0F : 1 / float32(pixel.Samples);
        const auto radiance = (pixel.Emission.Value + pixel.Direct.Value + pixel.Indirect.Value) * scale;
        auto* target = output + headerBytes + i * pixelBytes;
        PutFloat(target, radiance.X);
        PutFloat(target + 4, radiance.Y);
        PutFloat(target + 8, radiance.Z);
        Put32(target + 12, pixel.Samples);
    }
    written = size;
    return Status::Ready;
}
} // namespace ludus::graphics::renderer
