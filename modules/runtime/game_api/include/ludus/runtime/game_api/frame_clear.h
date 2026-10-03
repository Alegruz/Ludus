#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::runtime::game_api
{
// Host-owned configuration resource, independent of module AssetReload support.
// Cooked format: "LCLR", little-endian uint32 version 1, four little-endian
// IEEE float32 RGBA values in [0,1]. Exactly 24 bytes; no native struct dumps.
inline constexpr foundation::uint64 kFrameClearAssetId = 0x1000000000000001ULL;
inline constexpr foundation::usize kFrameClearArtifactBytes = 24;
} // namespace ludus::runtime::game_api
