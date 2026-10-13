#pragma once

#include "reference_data.h"

#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/graphics/renderer/gpu_preview.hpp>
#include <ludus/graphics/rhi/compute.h>
#include <ludus/graphics/rhi/graph.h>

namespace ludus::graphics::renderer::internal
{
struct GpuPreviewUniform final
{
    math::Matrix4 ClipToWorld;
    math::Vector4 CameraPositionWorld;
    uint32 Dimensions[4]{};
    uint32 Offsets[4]{};
    uint32 Counts[4]{};
    uint32 Sampling[4]{};
    math::Vector4 Environment;
    math::Vector4 Options;
    math::Vector4 BoundsMinimumWorld, BoundsMaximumWorld;
    uint32 SurfaceLayout[4]{};
};
static_assert(sizeof(GpuPreviewUniform) == 224);
struct GpuFrameRecord final
{
    uint64 Generation = 1;
    bool Owned = false;
    rhi::TextureHandle Texture{};
    rhi::SubmissionToken Producer{};
    uint32 Samples = 0;
};
struct GpuPreviewRecord final
{
    uint64 Generation = 1;
    bool Used = false, Owned = false;
    rhi::BufferHandle Data{}, Texels{}, Parameters{}, Accumulation{}, Colors{}, ImageParameters{};
    rhi::BindingSetHandle ComputeBindings{}, ImageBindings{};
    GpuFrameRecord Frames[2];
    GpuPreviewInfo Info;
    uint32 Width = 0, Height = 0, PendingFrame = 0, LatestFrame = 0;
    bool HasImage = false;
    usize DataBytes = 0, TexelBytes = 0;
    bool ComputeBindingsCreated = false, ImageBindingsCreated = false;
    rhi::RasterStatus Setup = rhi::RasterStatus::Pending;
    usize GpuBytes = 0;
};
struct GpuPreviewState final
{
    GpuPreviewOptions Options;
    uint32 Phase = 0;
    bool QuadCreated = false, IndicesCreated = false;
    rhi::RasterStatus Setup = rhi::RasterStatus::Pending;
    rhi::RasterShaderHandle ComputeShader{}, VertexShader{}, FragmentShader{};
    rhi::BindingLayoutHandle ComputeLayout{}, ImageLayout{};
    rhi::ComputePipelineHandle ComputePipeline{};
    rhi::RasterPipelineHandle ImagePipeline{};
    rhi::BufferHandle Quad{}, Indices{};
    GpuPreviewRecord Jobs[4];
    usize GpuBytes = 0;
};
} // namespace ludus::graphics::renderer::internal
