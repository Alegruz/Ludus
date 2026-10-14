#pragma once

#include <ludus/content/content.h>
#include <ludus/content/scene_cook/scene_cook.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <string_view>

/// @file
/// Best-effort pbrt-v4 scene importer (profile pbrt-best-effort-v1).
///
/// Unlike the frozen pbrt-reference-v1 staircase2 cooker, this importer accepts
/// general pbrt-v4 scenes with reported approximations: unsupported
/// features become documented fallbacks, and the scene is fitted to the LUSCN
/// limits in Ludus::SceneFormat (6 meshes, 8 materials, 8 textures, 500 k
/// vertices, 128 objects, 64 MiB). Every compromise is recorded in a
/// deterministic, versioned JSON import report. Authored lighting that cannot fit
/// without losing sources or energy rejects explicitly. See
/// docs/development/best-effort-scene-import.md.
namespace ludus::content::scene_cook
{
/// Profile identifier recorded in the import report and the catalog.
inline constexpr std::string_view BEST_EFFORT_PROFILE = "pbrt-best-effort-v1";
/// Version of the import report JSON schema.
inline constexpr uint32 IMPORT_REPORT_SCHEMA = 1;

/// @brief Options for ImportPbrtBestEffort.
struct BestEffortOptions final
{
    /// Parse and plan only: no image decoding and no bundle. The report is complete
    /// except for texture sizes, and BestEffortResult::Bundle stays empty.
    bool IsDryRun = false;
};

/// @brief Headline numbers of one import, also present in the report.
struct ImportSummary final
{
    /// Source files read (the entry file plus includes).
    uint32 SourceFiles = 0;
    /// Shape placements after instance expansion.
    uint32 SourcePlacements = 0;
    /// Vertices across all placements before budgeting.
    uint32 SourceVertices = 0;
    /// Materials defined by the source (named and anonymous Material directives).
    uint32 SourceMaterials = 0;
    /// Image and spectrum textures defined by the source.
    uint32 SourceTextures = 0;
    /// AreaLightSource directives (each makes the shapes after it in its block emissive).
    uint32 SourceAreaLights = 0;
    /// Meshes in the cooked bundle (one per material slot).
    uint32 CookedMeshes = 0;
    /// Materials in the cooked bundle.
    uint32 CookedMaterials = 0;
    /// Textures in the cooked bundle, swatches included.
    uint32 CookedTextures = 0;
    /// Objects in the cooked bundle.
    uint32 CookedObjects = 0;
    /// Vertices across all cooked meshes.
    uint32 CookedVertices = 0;
    /// Placements left out by the vertex/index budget.
    uint32 DroppedPlacements = 0;
    /// Bundle size in bytes (zero for a dry run).
    usize BundleBytes = 0;
    /// Warning-level diagnostics (occurrences).
    uint32 Warnings = 0;
    /// Error-level diagnostics (occurrences); nonzero means the import failed.
    uint32 Errors = 0;
};

/// @brief Outputs of ImportPbrtBestEffort.
struct BestEffortResult final
{
    /// LUSCN version 3 bundle with authored photometric lighting and triangle bake coordinates (scene.luscn); empty for
    /// a dry run.
    foundation::Array<uint8> Bundle;
    /// Import report JSON (import-report.json) including its trailing newline.
    foundation::Array<uint8> Report;
    /// Verbatim LICENSE.txt (or LICENSE.md) from the import root; empty when absent.
    foundation::Array<uint8> License;
    /// Headline numbers.
    ImportSummary Summary;
    /// Root-relative paths of every file the import read (pbrt sources, PLY files,
    /// referenced images and the licence), without duplicates, in first-use order.
    /// Callers re-hash these to detect a source that changed after the import.
    foundation::Array<foundation::String> Inputs;
    /// SHA-256 of the exact bytes the import consumed from each Inputs entry (parallel array).
    foundation::Array<Digest> InputDigests;
};

/// Import a pbrt-v4 entry file with the best-effort profile.
/// @param entryFile Native path of the .pbrt file. Its directory is the import root;
/// Include/Import, PLY and image paths must resolve inside it.
/// @param options Dry-run selection.
/// @param outResult Replaced on success. On a parse failure with diagnostics,
/// outResult.Report still holds the report so the caller can show why.
/// @param outError Set on failure: Invalid (syntax errors or nothing drawable),
/// NotFound/IoError (entry file), Cancelled, OutOfMemory.
/// @param cancelOrNull Optional cancellation hook polled per directive and per placement.
/// @return true on success.
/// @note Synchronous file I/O on Linux and macOS; other targets report Unsupported
/// until the browser source file system lands. Deterministic: equal inputs give equal bytes.
[[nodiscard]] bool ImportPbrtBestEffort(std::string_view entryFile,
                                        const BestEffortOptions& options,
                                        BestEffortResult& outResult,
                                        CookError& outError,
                                        const Cancellation* cancelOrNull = nullptr) noexcept;

/// Write scene.luscn, import-report.json and (when present) LICENSE.txt below output.
/// @param output Native directory; created with its parents when absent.
/// @param result A non-dry-run result.
/// @param outError Set on failure.
/// @return true when every file was atomically replaced.
[[nodiscard]] bool
WriteBestEffortImport(std::string_view output, const BestEffortResult& result, CookError& outError) noexcept;
} // namespace ludus::content::scene_cook
