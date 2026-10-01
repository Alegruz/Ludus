#pragma once

// Private interop for the W5/W6 sample renderers. No WebGPU types enter the SDK.
// Borrow device/format only after Ready; borrow pass only during an open frame.
#include <webgpu/webgpu.h>
namespace ludus::graphics::rhi::backend
{
WGPUDevice ProbeDevice() noexcept;
WGPUTextureFormat ProbeFormat() noexcept;
WGPURenderPassEncoder ProbePass() noexcept;
} // namespace ludus::graphics::rhi::backend
