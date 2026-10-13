#pragma once

// Slang translates these explicit descriptor slots into SPIR-V, Metal, WGSL
// and GLES interfaces. "vk" is Slang's slot annotation, not a Vulkan runtime
// branch. Keep target-specific interface vocabulary in this RHI-owned boundary.
#define LUDUS_SHADER_BINDING(slot) [[vk::binding(slot, 0)]]
#define LUDUS_SHADER_LOCATION(slot) [[vk::location(slot)]]
