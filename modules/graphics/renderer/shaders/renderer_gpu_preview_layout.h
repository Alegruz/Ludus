#pragma once

// Private packed-scene ABI, shared verbatim by host packing and Slang admission.
// Strides count float4 words, not bytes. Update the version when changing layout.
#define LUDUS_GPU_PREVIEW_SURFACE_WORDS 8
#define LUDUS_GPU_PREVIEW_LIGHT_WORDS 4
#define LUDUS_GPU_PREVIEW_KERNEL_VERSION 2
