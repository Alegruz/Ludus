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

// Addressed-random dimension schema shared by the CPU reference path and the GPU
// preview: dimensions 0-1 jitter the primary ray, and scattering event d owns the
// PER_BOUNCE dimensions starting at FIRST_BOUNCE + d * PER_BOUNCE.
#define LUDUS_PATH_RANDOM_FIRST_BOUNCE_DIMENSION 2
#define LUDUS_PATH_RANDOM_DIMENSIONS_PER_BOUNCE 8
// Admitted MaxDepth limits. Both keep every bounce dimension within the 16-bit
// Dimension of addressed-random layout v1; host code static_asserts this.
#define LUDUS_REFERENCE_MAX_DEPTH 64
#define LUDUS_GPU_PREVIEW_MAX_DEPTH 8
