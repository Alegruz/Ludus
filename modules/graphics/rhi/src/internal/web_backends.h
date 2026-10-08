#pragma once

// Declarations of the two browser sub-backends compiled alongside the Auto
// dispatcher (rhi_web.cpp). Each sub-backend defines the full backend.h +
// resources.h contract inside its own leaf namespace so both can be linked into
// one browser artifact with no duplicate symbols. rhi_webgpu.cpp is compiled
// with LUDUS_RHI_WEBGPU_NAMESPACE=webgpu and rhi_webgl.cpp with
// LUDUS_RHI_WEBGL_NAMESPACE=webgl. The dispatcher routes to the active one.
#include "raster.h"
#include "resources.h"
#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/rhi.h>

#include <span>

#define LUDUS_RHI_DECLARE_WEB_BACKEND(ns)                                                                              \
    namespace ludus::graphics::rhi::ns                                                                                 \
    {                                                                                                                  \
    Backend Kind() noexcept;                                                                                           \
    bool Supports(BackendSelection) noexcept;                                                                          \
    StartupError                                                                                                       \
    Start(const ApplicationInfo&, const WindowInfo&, ludus::foundation::uint32, BackendSelection) noexcept;            \
    void Shutdown() noexcept;                                                                                          \
    FrameStatus SetTarget(const FrameTarget&) noexcept;                                                                \
    FrameStatus Begin() noexcept;                                                                                      \
    FrameStatus End() noexcept;                                                                                        \
    FrameInfo GetFrameInfo() noexcept;                                                                                 \
    ResourceStatus                                                                                                     \
    CreateShader(ludus::foundation::usize, const ShaderDescription&, ludus::foundation::uint32) noexcept;              \
    ResourceStatus                                                                                                     \
    CreateUniform(ludus::foundation::usize, const backend::UniformDescription&, ludus::foundation::uint32) noexcept;   \
    ResourceStatus                                                                                                     \
    CreatePipeline(ludus::foundation::usize, const backend::PipelineResources&, ludus::foundation::uint32) noexcept;   \
    void DestroyShader(ludus::foundation::usize) noexcept;                                                             \
    void DestroyUniform(ludus::foundation::usize) noexcept;                                                            \
    void DestroyPipeline(ludus::foundation::usize) noexcept;                                                           \
    void UpdateUniform(ludus::foundation::usize, std::span<const ludus::foundation::uint8>) noexcept;                  \
    ResourceStatus Draw(ludus::foundation::usize) noexcept;                                                            \
    }

LUDUS_RHI_DECLARE_WEB_BACKEND(webgpu)
LUDUS_RHI_DECLARE_WEB_BACKEND(webgl)

#undef LUDUS_RHI_DECLARE_WEB_BACKEND

// WebGPU-only private interop (ProbeDevice/Format/Pass) for the W5/W6 sample
// renderers. On the Auto build these live in backend::webgpu and the dispatcher
// re-exports backend::Probe* (declared by webgpu_probe.h) by forwarding here.
#include <webgpu/webgpu.h>
namespace ludus::graphics::rhi::webgpu
{
WGPUDevice ProbeDevice() noexcept;
WGPUTextureFormat ProbeFormat() noexcept;
WGPURenderPassEncoder ProbePass() noexcept;
} // namespace ludus::graphics::rhi::webgpu

namespace ludus::graphics::rhi::webgpu
{
RasterCapabilities RasterLimits() noexcept;
RasterStatus RasterCreateBuffer(foundation::usize,
                                const BufferDescription&,
                                std::span<const foundation::uint8>,
                                foundation::uint32) noexcept;
RasterStatus
RasterCreateTexture(foundation::usize, const TextureDescription&, const TextureUpload&, foundation::uint32) noexcept;
RasterStatus RasterCreateView(foundation::usize, const internal::RasterViewSource&, foundation::uint32) noexcept;
RasterStatus RasterCreateSampler(foundation::usize, const SamplerDescription&, foundation::uint32) noexcept;
RasterStatus RasterCreateShader(foundation::usize,
                                const ShaderDescription&,
                                const internal::RasterShaderInfo&,
                                foundation::uint32) noexcept;
RasterStatus RasterCreateLayout(foundation::usize, const internal::RasterLayout&, foundation::uint32) noexcept;
RasterStatus RasterCreateSet(foundation::usize,
                             const internal::RasterLayout&,
                             const internal::RasterSet&,
                             foundation::uint32) noexcept;
RasterStatus RasterCreatePipeline(foundation::usize, const internal::RasterPipelineInfo&, foundation::uint32) noexcept;
void RasterDestroy(internal::RasterKind, foundation::usize) noexcept;
RasterStatus RasterDraw(const internal::RasterPacket&) noexcept;
// Reserve mandatory completion bookkeeping before the first draw; submission
// cannot discover that its callback storage is exhausted after GPU work starts.
RasterStatus RasterReserveSubmission() noexcept;
void RasterSubmit(foundation::uint64 ordinal) noexcept;
foundation::uint64 RasterCompleted() noexcept;
void RasterReset() noexcept;
void RasterShutdown() noexcept;
} // namespace ludus::graphics::rhi::webgpu
namespace ludus::graphics::rhi::webgl
{
RasterCapabilities RasterLimits() noexcept;
RasterStatus RasterCreateBuffer(foundation::usize,
                                const BufferDescription&,
                                std::span<const foundation::uint8>,
                                foundation::uint32) noexcept;
RasterStatus
RasterCreateTexture(foundation::usize, const TextureDescription&, const TextureUpload&, foundation::uint32) noexcept;
RasterStatus RasterCreateView(foundation::usize, const internal::RasterViewSource&, foundation::uint32) noexcept;
RasterStatus RasterCreateSampler(foundation::usize, const SamplerDescription&, foundation::uint32) noexcept;
RasterStatus RasterCreateShader(foundation::usize,
                                const ShaderDescription&,
                                const internal::RasterShaderInfo&,
                                foundation::uint32) noexcept;
RasterStatus RasterCreateLayout(foundation::usize, const internal::RasterLayout&, foundation::uint32) noexcept;
RasterStatus RasterCreateSet(foundation::usize,
                             const internal::RasterLayout&,
                             const internal::RasterSet&,
                             foundation::uint32) noexcept;
RasterStatus RasterCreatePipeline(foundation::usize, const internal::RasterPipelineInfo&, foundation::uint32) noexcept;
void RasterDestroy(internal::RasterKind, foundation::usize) noexcept;
RasterStatus RasterDraw(const internal::RasterPacket&) noexcept;
// Reserve mandatory completion bookkeeping before the first draw; submission
// cannot discover that its callback storage is exhausted after GPU work starts.
RasterStatus RasterReserveSubmission() noexcept;
void RasterSubmit(foundation::uint64 ordinal) noexcept;
foundation::uint64 RasterCompleted() noexcept;
void RasterReset() noexcept;
void RasterShutdown() noexcept;
} // namespace ludus::graphics::rhi::webgl
