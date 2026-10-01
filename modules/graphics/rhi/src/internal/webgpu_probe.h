#pragma once

// Private interop for the W5 smoke probe only. No WebGPU types enter the SDK.
#include <webgpu/webgpu.h>
namespace ludus::graphics::rhi::backend
{
WGPUDevice ProbeDevice() noexcept;
WGPUTextureFormat ProbeFormat() noexcept;
WGPURenderPassEncoder ProbePass() noexcept;
} // namespace ludus::graphics::rhi::backend
