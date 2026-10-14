#pragma once

#include <ludus/foundation/base/types.h>

/// @file
/// Versioned numeric render-plan records shared by offline tools and Renderer.
/// Generated C++ plans use these records directly. A file codec must write each
/// field in little-endian order, never copy host struct bytes. No GPU ownership.
namespace ludus::content::render_graph_format
{
/// Initial static, opaque, instanced raster execution profile.
inline constexpr foundation::uint32 VERSION = 1;
/// Maximum authored passes in one admitted configuration.
inline constexpr foundation::usize MAX_PASSES = 16;
/// Maximum logical resources; depth may alias a color allocation's private aspect.
inline constexpr foundation::usize MAX_RESOURCES = 32;
/// Maximum camera inputs per plan; indices are stable within its generation.
inline constexpr foundation::usize MAX_CAMERAS = 8;
/// Largest supported scene extraction in this initial profile.
inline constexpr foundation::usize MAX_ITEMS = 128;
/// Largest supported mesh table in one scene replacement.
inline constexpr foundation::usize MAX_MESHES = 32;
/// Explicit finite technique count; no output-connectivity specialization.
inline constexpr foundation::usize TECHNIQUE_COUNT = 2;
/// Resource or input index absent from the declared plan.
inline constexpr foundation::uint32 ABSENT = ~foundation::uint32{0};
/// Exact compiled program role, independent of graph instance and viewport size.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class Technique : foundation::uint32
{
    ColorAndDepth, ///< Authored color plus depth stage pair.
    DepthOnly,     ///< Authored void fragment plus identical static geometry path.
};
/// Logical image aspect; physical depth is owned by its Backing allocation.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class Aspect : foundation::uint32
{
    Color, ///< Linear RGBA8 color.
    Depth, ///< Depth32Float private depth aspect.
};
/// Initialization of the complete attachment, before viewport/scissor clipping.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class Load : foundation::uint32
{
    Clear,   ///< Define the full attachment.
    Load,    ///< Preserve initialized earlier contents.
    Discard, ///< Earlier contents are undefined.
};
/// Contents retained after execution.
// NOLINTNEXTLINE(performance-enum-size): versioned numeric fields have explicit 32-bit widths.
enum class Store : foundation::uint32
{
    Store,   ///< Preserve defined output.
    Discard, ///< Output becomes undefined.
};
/// Logical image requirement. Extents are pixels; no runtime native handle.
struct Resource final
{
    foundation::uint32 Width;      ///< Positive physical width.
    foundation::uint32 Height;     ///< Positive physical height.
    Aspect Kind;                   ///< Color or depth aspect.
    foundation::uint32 Backing;    ///< Index of the color/private-depth allocation owner; may name itself.
    foundation::uint32 IsSurface;  ///< Zero or one; owner is the acquired presentation surface.
    foundation::uint32 IsExported; ///< Zero or one; output may be leased after producer completion.
};
/// Required destination for one active camera, verified independently of pass coverage.
struct Camera final
{
    foundation::uint32 Target; ///< Logical surface color or exported color/depth resource.
};
/// One retained scene raster operation; all indices are checked before GPU setup.
struct Pass final
{
    Technique Program;         ///< Explicit authored shader choice.
    foundation::uint32 Camera; ///< Index into the current camera extraction.
    foundation::uint32 Color;  ///< Logical color resource, or ABSENT for DepthOnly.
    foundation::uint32 Depth;  ///< Logical depth resource.
    Load ColorLoad;            ///< For DepthOnly, declares compatibility color initialization without a logical output.
    Store ColorStore;          ///< For DepthOnly, controls compatibility allocation color retention.
    Load DepthLoad;            ///< Depth initialization.
    Store DepthStore;          ///< Depth validity after this operation.
    foundation::uint32 AllowsRepeatedCoverage; ///< Zero or one; explicitly permits repeated rendering in a role/view.
};
/// Header for a generated configuration; limits include unexecuted root effects.
struct Header final
{
    foundation::uint32 Version;       ///< VERSION.
    foundation::uint32 ResourceCount; ///< Bounded resource table size.
    foundation::uint32 PassCount;     ///< Bounded raster operation count.
    foundation::uint32 CameraCount;   ///< Required current camera input count.
};
static_assert(sizeof(Resource) == sizeof(foundation::uint32) * 6);
static_assert(sizeof(Pass) == sizeof(foundation::uint32) * 9);
static_assert(sizeof(Camera) == sizeof(foundation::uint32));
static_assert(sizeof(Header) == sizeof(foundation::uint32) * 4);
} // namespace ludus::content::render_graph_format
