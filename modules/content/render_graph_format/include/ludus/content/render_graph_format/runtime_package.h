#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/content/render_graph_format/plan.h>

/// @file
/// Versioned runtime generation (`.lurgrt`) layout for RG3 atomic activation.
/// The offline tool writes every field in little-endian order; decoders read it
/// field by field and never copy host struct bytes. The file carries the same
/// numeric plan records as a compiled RG2 header, plus cooked stage artifacts and
/// the CPU interface identity. It contains no source text that needs parsing.
///
/// Layout: a header of HEADER_FIELD_COUNT uint32 values (magic, version,
/// resource/pass/camera/stage counts) followed by the interface and generation
/// identities; ResourceCount resource records, CameraCount camera records and
/// PassCount pass records; then StageCount stage records. A stage record holds
/// STAGE_FIELD_COUNT uint32 values (role, target, input count, entry bytes,
/// artifact bytes), its input pairs, the entry name and the artifact bytes, with
/// the entry and artifact each zero-padded to a four-byte boundary. No trailing
/// bytes are permitted.
namespace ludus::content::render_graph_format
{
/// "LURG" read as a little-endian uint32.
inline constexpr foundation::uint32 PACKAGE_MAGIC = 0x4752554CU;
/// Runtime generation layout version.
inline constexpr foundation::uint32 PACKAGE_VERSION = 1;
/// SHA-256 digest width of interface and generation identities.
inline constexpr foundation::usize IDENTITY_BYTES = 32;
/// Largest accepted runtime generation.
inline constexpr foundation::usize MAX_PACKAGE_BYTES = foundation::usize{16} * 1024 * 1024;
/// Largest single stage artifact, matching the RG1 per-artifact bound.
inline constexpr foundation::usize MAX_ARTIFACT_BYTES = foundation::usize{2} * 1024 * 1024;
/// Largest stage entry-point name.
inline constexpr foundation::usize MAX_ENTRY_BYTES = 63;
/// Number of explicit stage roles (two per technique).
inline constexpr foundation::usize STAGE_ROLE_COUNT = 4;
/// Number of artifact targets a generation may carry.
inline constexpr foundation::usize SHADER_TARGET_COUNT = 4;
/// Largest stage table: one artifact per role and target.
inline constexpr foundation::usize MAX_STAGES = STAGE_ROLE_COUNT * SHADER_TARGET_COUNT;
/// Renderer static vertex/instance interface: position plus four transform rows and color.
inline constexpr foundation::usize STATIC_INPUT_COUNT = 6;
/// Fixed uint32 fields before the identities.
inline constexpr foundation::usize HEADER_FIELD_COUNT = 6;
/// Serialized header width.
inline constexpr foundation::usize HEADER_BYTES = HEADER_FIELD_COUNT * sizeof(foundation::uint32) + 2 * IDENTITY_BYTES;
/// Serialized resource record width; matches Resource's six fields.
inline constexpr foundation::usize RESOURCE_RECORD_BYTES = 6 * sizeof(foundation::uint32);
/// Serialized camera record width; matches Camera's one field.
inline constexpr foundation::usize CAMERA_RECORD_BYTES = sizeof(foundation::uint32);
/// Serialized pass record width; matches Pass's nine fields.
inline constexpr foundation::usize PASS_RECORD_BYTES = 9 * sizeof(foundation::uint32);
/// Fixed uint32 fields at the start of each stage record.
inline constexpr foundation::usize STAGE_FIELD_COUNT = 5;
/// Serialized width of one (location, format) stage input pair.
inline constexpr foundation::usize INPUT_RECORD_BYTES = 2 * sizeof(foundation::uint32);
/// Explicit program role; Color* belong to ColorAndDepth, Depth* to DepthOnly.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class StageRole : foundation::uint32
{
    ColorVertex,   ///< ColorAndDepth geometry entry.
    ColorFragment, ///< ColorAndDepth color entry.
    DepthVertex,   ///< DepthOnly geometry entry.
    DepthFragment, ///< DepthOnly void coverage entry.
};
/// Artifact representation carried by one stage record.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class ShaderTarget : foundation::uint32
{
    Metal,  ///< Metal Shading Language source text.
    Spirv,  ///< SPIR-V words; the byte length is a multiple of four.
    Wgsl,   ///< WGSL source text.
    GlslEs, ///< GLSL ES 3.00 source text.
};
/// Float vertex input width of one stage input record.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class InputFormat : foundation::uint32
{
    Float32, ///< One float.
    Float2,  ///< Two floats.
    Float3,  ///< Three floats.
    Float4,  ///< Four floats.
};
static_assert(HEADER_BYTES == 88);
static_assert(RESOURCE_RECORD_BYTES == sizeof(Resource));
static_assert(CAMERA_RECORD_BYTES == sizeof(Camera));
static_assert(PASS_RECORD_BYTES == sizeof(Pass));
} // namespace ludus::content::render_graph_format
