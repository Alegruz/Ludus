#pragma once

#include <ludus/foundation/base/types.h>

#include <type_traits>

namespace ludus::shader_probe
{
using namespace foundation;

// Independent CPU contracts. The compiler output verifier checks each target
// against these offsets; equal layouts in this shader are not a general promise.
struct alignas(16) VulkanUniforms final
{
    float32 Resolution[2];
    float32 ElapsedTime;
    float32 Padding0;
    float32 Direction[3];
    float32 Padding1;
    float32 Tint[4];
};
struct alignas(16) WebGpuUniforms final
{
    float32 Resolution[2];
    float32 ElapsedTime;
    float32 Padding0;
    float32 Direction[3];
    float32 Padding1;
    float32 Tint[4];
};
static_assert(std::is_standard_layout_v<VulkanUniforms>);
static_assert(offsetof(VulkanUniforms, Resolution) == 0);
static_assert(offsetof(VulkanUniforms, ElapsedTime) == 8);
static_assert(offsetof(VulkanUniforms, Direction) == 16);
static_assert(offsetof(VulkanUniforms, Tint) == 32);
static_assert(sizeof(VulkanUniforms) == 48 && alignof(VulkanUniforms) == 16);
static_assert(std::is_standard_layout_v<WebGpuUniforms>);
static_assert(offsetof(WebGpuUniforms, Resolution) == 0);
static_assert(offsetof(WebGpuUniforms, ElapsedTime) == 8);
static_assert(offsetof(WebGpuUniforms, Direction) == 16);
static_assert(offsetof(WebGpuUniforms, Tint) == 32);
static_assert(sizeof(WebGpuUniforms) == 48 && alignof(WebGpuUniforms) == 16);
} // namespace ludus::shader_probe
