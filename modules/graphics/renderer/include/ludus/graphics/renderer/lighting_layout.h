#pragma once

#ifdef __cplusplus
#    include <ludus/foundation/base/types.h>
namespace ludus::graphics::renderer
{
/// Exact unsigned 32-bit scalar for host/shader layout constants.
using LightingLayoutValue = foundation::uint32;
#else
/// Unsigned 32-bit scalar for the shader declarations of layout constants.
typedef uint LightingLayoutValue;
#endif

/// Maximum shadow atlas regions, shared verbatim by host and shader layouts.
static const LightingLayoutValue SHADOW_REGION_CAPACITY = 80;
/// Float4 words per shadow projection: four matrix rows, rectangle and settings.
static const LightingLayoutValue SHADOW_REGION_WORDS = 6;
/// Total Float4 words in the shadow uniform array.
static const LightingLayoutValue SHADOW_DATA_WORDS = SHADOW_REGION_CAPACITY * SHADOW_REGION_WORDS;
/// Frozen CPU/compute transport profile; version 4 adds geometric emission sidedness.
static const LightingLayoutValue LIGHTING_PROFILE_VERSION = 4;

#ifdef __cplusplus
} // namespace ludus::graphics::renderer
#endif
