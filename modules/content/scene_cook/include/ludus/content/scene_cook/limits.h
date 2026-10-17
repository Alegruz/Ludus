#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::content::scene_cook
{
using foundation::usize;

/// Largest source file admitted by the cooker (bundles are bounded by scene_format::MAX_BUNDLE_BYTES).
inline constexpr usize MAX_SOURCE_BYTES = usize{64} * 1024 * 1024;
/// Inclusive source byte ceiling for the strict reference profile's entry document.
inline constexpr usize MAX_REFERENCE_DOCUMENT_BYTES = usize{1024} * 1024;
/// Inclusive source byte ceiling for the strict reference profile's license text.
inline constexpr usize MAX_LICENSE_BYTES = usize{1024} * 1024;
/// Largest artifact, bundle or resident closure in bytes.
inline constexpr usize MAX_ASSET_BYTES = usize{64} * 1024 * 1024;
/// Largest level or metadata document in bytes.
inline constexpr usize MAX_METADATA_BYTES = usize{1024} * 1024;
/// Largest project catalog (assets/catalog.json) in bytes. Every artifact's build
/// record carries the full source_inputs digest map of its import (it feeds the
/// frozen artifact key), so a catalog grows with resources x source files:
/// two imports of ~800-file pbrt scenes already pass 1 MiB. Matches the
/// artifact limit rather than the small-document limit.
inline constexpr usize MAX_CATALOG_BYTES = usize{64} * 1024 * 1024;

} // namespace ludus::content::scene_cook
