#pragma once

#include <ludus/content/content.h>
#include <ludus/foundation/containers/array.hpp>

#include <string_view>

/// @file
/// Native pbrt-reference-v1 scene cooker (Ludus::SceneCook).
///
/// Byte-identical port of the retired Python `scene_cook.py`: the explicit
/// staircase2 PBRT v4 subset with binary PLY and uncompressed TGA inputs is
/// cooked into a bounded LUSCN v1 sample bundle, a manifest and the retained
/// licence text. Outputs match the Python 3.12 reference byte for byte; see
/// tests/goldens/scene_pipeline.
namespace ludus::content::scene_cook
{
/// Profile identifier of the cooked bundle layout and manifest.
inline constexpr std::string_view PROFILE = "pbrt-reference-v1";
/// Largest source file admitted by the cooker (bundles are bounded by scene_format::MAX_BUNDLE_BYTES).
inline constexpr usize MAX_SOURCE_BYTES = usize{64} * 1024 * 1024;

/// @brief Cooperative cancellation hook polled between bounded work units.
struct Cancellation final
{
    /// Returns true when the cook should stop; must be thread-safe for the caller.
    bool (*Requested)(void* context) noexcept = nullptr;
    /// Opaque pointer passed back to Requested.
    void* Context = nullptr;
};

/// @brief Failure description with the reference converter's message text.
struct CookError final
{
    /// Invalid for profile violations, Conflict when a source changed between
    /// reads, Cancelled, Limit, NotFound, IoError or OutOfMemory otherwise.
    Status Result = Status::Ok;
    /// Human-readable message, matching the reference wording where it exists.
    MessageText Message;
};

/// @brief In-memory cook outputs; written to disk by WriteCook.
struct CookResult final
{
    /// LUSCN v1 bundle bytes (scene.luscn).
    foundation::Array<uint8> Bundle;
    /// Manifest JSON (manifest.json) including its trailing newline.
    foundation::Array<uint8> Manifest;
    /// Verbatim licence bytes (LICENSE.txt).
    foundation::Array<uint8> License;
};

/// Cook one source directory containing scene-v4.pbrt and LICENSE.txt.
/// @param source Native directory path; resolved before use, and its base name
/// is recorded when the licence does not identify the staircase2 author.
/// @param outResult Replaced on success; contents are unspecified on failure.
/// @param outError Set on failure.
/// @param cancelOrNull Optional cancellation hook; nullptr disables cancellation.
/// @return true on success.
/// @note Synchronous file I/O. Symlinks below the source root are rejected.
[[nodiscard]] bool Cook(std::string_view source,
                        CookResult& outResult,
                        CookError& outError,
                        const Cancellation* cancelOrNull = nullptr) noexcept;

/// Write scene.luscn, manifest.json and LICENSE.txt below output.
/// @param output Native directory; created with its parents when absent.
/// @param result Outputs from Cook.
/// @param outError Set on failure.
/// @return true when every file was atomically replaced.
[[nodiscard]] bool WriteCook(std::string_view output, const CookResult& result, CookError& outError) noexcept;
} // namespace ludus::content::scene_cook
