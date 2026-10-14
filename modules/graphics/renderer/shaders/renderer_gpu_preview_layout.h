#pragma once

#ifdef __cplusplus
#    include <ludus/graphics/renderer/lighting_layout.h>
#else
#    include "lighting_layout.h"
#endif

// Private packed-scene ABI, shared verbatim by host packing and Slang admission.
// Strides count float4 words, not bytes. Update the version when changing layout.
#define LUDUS_GPU_PREVIEW_SURFACE_WORDS 9
#define LUDUS_GPU_PREVIEW_LIGHT_WORDS 4
#define LUDUS_GPU_PREVIEW_KERNEL_VERSION LIGHTING_PROFILE_VERSION

#define LUDUS_GPU_PREVIEW_TRIANGLE_WORDS 16

#define LUDUS_GPU_PREVIEW_TEXTURES_PER_SURFACE 3
