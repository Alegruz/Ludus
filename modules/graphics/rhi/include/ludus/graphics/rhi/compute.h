#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::rhi
{
/// Opaque immutable compute pipeline in the shared RHI registry. Owner-thread
/// only; destruction invalidates public copies while retained graph/GPU uses live.
struct ComputePipelineHandle final
{
private:
    ludus::foundation::uint64 Owner = 0;
    ludus::foundation::uint64 Generation = 0;
    ludus::foundation::uint32 Slot = 0;
    /// Grants shared registry access to this incarnation.
    friend struct internal::RasterAccess;
};
/// Bounded single-queue buffer compute profile; zero fields mean unavailable.
/// Storage textures, atomics between queues and indirect dispatch are deferred.
/// Layouts contain at most four storage bindings and eight total buffer bindings.
struct ComputeCapabilities final
{
    /// Maximum storage binding range in bytes.
    ludus::foundation::usize MaxStorageRange = 0;
    /// Storage binding offset alignment in bytes.
    ludus::foundation::usize StorageOffsetAlignment = 0;
    /// Maximum local threads along each axis.
    ludus::foundation::uint32 MaxWorkgroupSize[3]{};
    /// Maximum product of local workgroup dimensions.
    ludus::foundation::uint32 MaxWorkgroupInvocations = 0;
    /// Maximum dispatched workgroups along each axis.
    ludus::foundation::uint32 MaxDispatch[3]{};
};
/// Immutable compute stage and exact group-zero layout; dependencies must be ready.
struct ComputePipelineDescription final
{
    /// Reflected Compute shader, including verified local workgroup dimensions.
    RasterShaderHandle Shader{};
    /// Immutable layout with Compute-only visibility and buffer entries.
    BindingLayoutHandle Layout{};
};
/// One logical compute pass contains exactly one dispatch. Dependent dispatches
/// need separate ordered graph passes. Shader-local races remain kernel obligations.
struct ComputeDispatch final
{
    /// Ready immutable pipeline.
    ComputePipelineHandle Pipeline{};
    /// Ready binding snapshot with exactly the pipeline layout.
    BindingSetHandle Bindings{};
    /// Number of workgroups, nonzero and bounded on every axis.
    ludus::foundation::uint32 Groups[3]{1, 1, 1};
};
/// Indexed indirect command ABI shared by Vulkan, Metal and WebGPU: twenty bytes.
/// The producing kernel must keep counts within the associated RasterDraw bounds
/// and write zero FirstIndex, BaseVertex and FirstInstance. IndexOffset in the
/// packet selects the mesh. Violating this contract is a kernel correctness bug.
struct IndexedIndirectArguments final
{
    /// Number of mesh indices, at most RasterDraw::IndexCount.
    ludus::foundation::uint32 IndexCount = 0;
    /// Number of instances, at most RasterDraw::InstanceCount; zero culls the draw.
    ludus::foundation::uint32 InstanceCount = 0;
    /// Must be zero in this bounded profile.
    ludus::foundation::uint32 FirstIndex = 0;
    /// Must be zero in this bounded profile.
    ludus::foundation::int32 BaseVertex = 0;
    /// Must be zero in this bounded profile.
    ludus::foundation::uint32 FirstInstance = 0;
};
/// Query negotiated compute limits. Invalid owner preserves output; other failures
/// zero it. WebGL2 returns Unsupported. No operation waits for the GPU.
[[nodiscard]] RasterStatus GetComputeCapabilities(DeviceHandle, ComputeCapabilities&) noexcept;
/// Create a retained immutable compute pipeline between frames. Failure preserves
/// null output; Pending publishes ownership requiring polling and destruction.
[[nodiscard]] RasterStatus
CreateComputePipeline(DeviceHandle, const ComputePipelineDescription&, ComputePipelineHandle&) noexcept;
/// Poll creation; invalid/stale/foreign identities return InvalidHandle.
[[nodiscard]] RasterStatus GetStatus(DeviceHandle, ComputePipelineHandle) noexcept;
/// Logically destroy between frames; graph and GPU leases delay physical release.
/// Invalid handle preserves output. Success clears it.
[[nodiscard]] RasterStatus Destroy(DeviceHandle, ComputePipelineHandle&) noexcept;
} // namespace ludus::graphics::rhi
