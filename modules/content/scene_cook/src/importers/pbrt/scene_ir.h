#pragma once

// SceneIR v1: the format-neutral intermediate representation produced by the
// best-effort front-ends (pbrt-v4 today) and consumed by the budget planner.
// Geometry stays in object space with an object-to-world transform, and
// ObjectBegin/ObjectInstance instancing is preserved so the planner decides
// whether to expand it. Materials are already mapped to the engine's PBR terms.

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/foundation/strings/string.hpp>

namespace ludus::content::scene_cook::importers
{
using foundation::Array;
using foundation::String;
using foundation::core::float32;
using foundation::core::float64;
using foundation::core::int32;
using foundation::core::uint32;
using foundation::core::uint8;
using foundation::core::usize;
namespace math = foundation::math;

/// Index value for "no entry" in IR cross references.
inline constexpr uint32 IR_NONE = ~uint32{0};

/// Colour encoding of a texture's source texels.
enum class TextureEncoding : uint8
{
    Srgb,
    Linear,
    Gamma,
};

/// A texture referenced by at least one material.
struct IrTexture final
{
    /// Declared name (pbrt `Texture "name"`).
    String Name;
    /// Root-relative path of the image file; empty for constant textures.
    String File;
    /// Constant colour for constant-valued textures, or the multiplier for images.
    float64 Scale[3] = {1.0, 1.0, 1.0};
    TextureEncoding Encoding = TextureEncoding::Srgb;
    float64 Gamma = 2.2;
    bool IsInverted = false;
    bool IsRepeated = true;
    /// Source location of the declaration.
    uint32 SourceFile = IR_NONE;
    uint32 Line = 0;
};

/// A surface material mapped to the engine's metallic-roughness model.
struct IrMaterial final
{
    /// Declared name; empty for anonymous `Material` directives.
    String Name;
    /// Source material type (diffuse, conductor, ...), kept for the report.
    String SourceType;
    /// Linear RGB base colour (multiplied with the texture when present).
    float64 BaseColour[3] = {.5, .5, .5};
    /// Index into SceneIr::Textures or IR_NONE.
    uint32 BaseColourTexture = IR_NONE;
    float64 Metallic = 0.0;
    float64 Roughness = 1.0;
    /// True when the source material had no faithful mapping.
    bool IsApproximated = false;
    uint32 SourceFile = IR_NONE;
    uint32 Line = 0;
};

/// Indexed triangle mesh in object space.
struct IrMesh final
{
    /// xyz triples.
    Array<float32> Positions;
    /// xyz triples, or empty when the source has none (computed by the planner).
    Array<float32> Normals;
    /// uv pairs with v already flipped to the LUSCN top-down convention, or empty.
    Array<float32> Uvs;
    /// Triangle list.
    Array<uint32> Indices;

    [[nodiscard]] usize GetVertexCount() const noexcept
    {
        return Positions.GetSize() / 3;
    }
};

/// One placed shape: a mesh with its material, emission and transform.
struct IrShape final
{
    uint32 Mesh = IR_NONE;
    /// Index into SceneIr::Materials (IR_NONE selects the default material).
    uint32 Material = IR_NONE;
    /// Linear RGB radiance from an enclosing AreaLightSource; zero otherwise.
    float64 Emission[3] = {0.0, 0.0, 0.0};
    /// Object-to-world (or object-to-instance inside ObjectBegin).
    math::Matrix4d Transform = math::Matrix4d::Identity();
    /// ReverseOrientation combined with a negative-determinant transform.
    bool IsWindingFlipped = false;
    uint32 SourceFile = IR_NONE;
    uint32 Line = 0;
};

/// A named ObjectBegin/ObjectEnd shape group.
struct IrObject final
{
    String Name;
    /// Indices into SceneIr::Shapes owned by this object (not placed in the world directly).
    Array<uint32> Shapes;
};

/// One ObjectInstance placement.
struct IrInstance final
{
    uint32 Object = IR_NONE;
    /// Instance-to-world.
    math::Matrix4d Transform = math::Matrix4d::Identity();
};

/// Camera derived from the pbrt Camera directive.
struct IrCamera final
{
    bool IsPresent = false;
    math::Vector3d Eye{0.0, 0.0, 0.0};
    math::Vector3d Target{0.0, 0.0, 1.0};
    math::Vector3d Up{0.0, 1.0, 0.0};
    /// Vertical field of view in radians.
    float64 VerticalFov = 0.785398163397448;
};

/// Environment fallback for infinite lights.
struct IrEnvironment final
{
    bool IsPresent = false;
    /// Mean linear RGB radiance (L x scale; image maps are averaged when decodable).
    float64 Radiance[3] = {0.0, 0.0, 0.0};
    /// Root-relative path of the environment image, empty for constant L.
    String File;
};

/// Light sources that have no LUSCN representation, counted by kind.
struct IrLightCounts final
{
    uint32 Area = 0;
    uint32 Infinite = 0;
    uint32 Point = 0;
    uint32 Spot = 0;
    uint32 Distant = 0;
    uint32 Other = 0;
};

/// Complete best-effort scene description.
struct SceneIr final
{
    /// Root-relative source files in first-visit order; shapes refer to them by index.
    Array<String> SourceFiles;
    Array<IrTexture> Textures;
    Array<IrMaterial> Materials;
    Array<IrMesh> Meshes;
    Array<IrShape> Shapes;
    /// Shapes placed directly in the world (not owned by an object).
    Array<uint32> WorldShapes;
    Array<IrObject> Objects;
    Array<IrInstance> Instances;
    IrCamera Camera;
    IrEnvironment Environment;
    IrLightCounts Lights;
    /// Film resolution when declared; zero otherwise.
    uint32 FilmWidth = 0;
    uint32 FilmHeight = 0;
};
} // namespace ludus::content::scene_cook::importers
