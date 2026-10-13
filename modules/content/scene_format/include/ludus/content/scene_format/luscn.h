#pragma once

#include <ludus/foundation/base/types.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

/// @file
/// LUSCN cooked scene interchange format (Ludus::SceneFormat).
///
/// This header is the single contract between producers of cooked scenes
/// (Ludus::SceneCook, the Editor World composer) and consumers
/// (Ludus::Resources SceneDefinition::Decode and the renderer that installs it).
/// It is owned by neither side: tools and runtime both depend on it, and it
/// depends on nothing but FoundationBase.
///
/// A bundle is little-endian and tightly packed, in this order:
/// Header; Header::TextureCount x (TextureRecord + RGBA8 bytes);
/// Header::MaterialCount x MaterialRecordV1 or MaterialRecordV2 (by Header::Version);
/// Header::MeshCount x (MeshRecord + MeshRecord::VertexCount x VertexRecord +
/// MeshRecord::IndexCount x uint32); Header::ObjectCount x ObjectRecord.
///
/// The record structs document the exact wire layout and are checked with
/// static_asserts; serialise them field by field in little-endian order rather
/// than copying the structs, so host endianness and padding never leak into files.
namespace ludus::content::scene_format
{
using foundation::core::float32;
using foundation::core::uint32;
using foundation::core::usize;

/// Eight-byte file magic, including the trailing NUL.
inline constexpr std::string_view MAGIC{"LUSCN01\0", 8};
/// Version 1: materials without an address mode (MaterialRecordV1).
inline constexpr uint32 VERSION_1 = 1;
/// Version 2: materials carry an explicit address mode (MaterialRecordV2).
inline constexpr uint32 VERSION_2 = 2;

/// Largest admitted bundle.
inline constexpr usize MAX_BUNDLE_BYTES = usize{64} * 1024 * 1024;
/// Mesh, material, texture and object count limits.
inline constexpr uint32 MAX_MESHES = 6;
inline constexpr uint32 MAX_MATERIALS = 8;
inline constexpr uint32 MAX_TEXTURES = 8;
inline constexpr uint32 MAX_OBJECTS = 128;
/// Texture limits: per-axis texels, bytes per texture and combined bytes.
inline constexpr uint32 MAX_TEXTURE_DIMENSION = 4096;
inline constexpr usize MAX_TEXTURE_BYTES = usize{16} * 1024 * 1024;
inline constexpr usize MAX_TOTAL_TEXTURE_BYTES = usize{32} * 1024 * 1024;
/// RGBA8 bytes per texel.
inline constexpr uint32 TEXEL_BYTES = 4;
/// Vertex limits per mesh and combined across meshes; index limit per mesh.
inline constexpr uint32 MAX_VERTICES = 500000;
inline constexpr uint32 MAX_INDICES = 1500000;
/// MaterialRecord::Texture value for an untextured surface.
inline constexpr uint32 NO_TEXTURE = ~uint32{0};
/// MaterialRecordV2::AddressMode values.
inline constexpr uint32 ADDRESS_CLAMP = 0;
inline constexpr uint32 ADDRESS_REPEAT = 1;
/// Admitted material ranges.
inline constexpr float32 MIN_ROUGHNESS = .045F;
inline constexpr float32 MAX_EMISSION = 10000.0F;

/// Bundle header.
struct Header final
{
    char Magic[8];        ///< MAGIC.
    uint32 Version;       ///< VERSION_1 or VERSION_2.
    uint32 MeshCount;     ///< 1..MAX_MESHES.
    uint32 MaterialCount; ///< 1..MAX_MATERIALS.
    uint32 TextureCount;  ///< 0..MAX_TEXTURES.
    uint32 ObjectCount;   ///< 1..MAX_OBJECTS.
    float32 Eye[3];       ///< Camera position in world units.
    float32 Target[3];    ///< Camera look-at target.
    float32 Up[3];        ///< Camera up direction.
    float32 VerticalFov;  ///< Vertical field of view in radians, in (0, pi).
};
/// Per-texture header, followed by ByteCount RGBA8 sRGB bytes (rows top to bottom).
struct TextureRecord final
{
    uint32 Width;     ///< 1..MAX_TEXTURE_DIMENSION.
    uint32 Height;    ///< 1..MAX_TEXTURE_DIMENSION.
    uint32 ByteCount; ///< Width * Height * TEXEL_BYTES.
};
/// Version 1 material.
struct MaterialRecordV1 final
{
    uint32 Texture;      ///< Texture index or NO_TEXTURE.
    float32 Metallic;    ///< [0, 1].
    float32 Roughness;   ///< [MIN_ROUGHNESS, 1].
    float32 Emission[3]; ///< Linear RGB radiance, [0, MAX_EMISSION].
};
/// Version 2 material: adds AddressMode after Texture.
struct MaterialRecordV2 final
{
    uint32 Texture;      ///< Texture index or NO_TEXTURE.
    uint32 AddressMode;  ///< ADDRESS_CLAMP or ADDRESS_REPEAT.
    float32 Metallic;    ///< [0, 1].
    float32 Roughness;   ///< [MIN_ROUGHNESS, 1].
    float32 Emission[3]; ///< Linear RGB radiance, [0, MAX_EMISSION].
};
/// Per-mesh header, followed by its vertices and uint32 triangle-list indices.
struct MeshRecord final
{
    uint32 VertexCount; ///< 1..MAX_VERTICES.
    uint32 IndexCount;  ///< 1..MAX_INDICES, a multiple of three.
};
/// Interleaved mesh vertex.
struct VertexRecord final
{
    float32 Position[3]; ///< Mesh-local position.
    float32 Normal[3];   ///< Nonzero shading normal.
    float32 Uv[2];       ///< Mip-zero texture coordinate.
};
/// Number of float32 components in a VertexRecord.
inline constexpr usize VERTEX_COMPONENTS = 8;
/// Placed mesh instance.
struct ObjectRecord final
{
    uint32 Mesh;            ///< Index into the meshes.
    uint32 Material;        ///< Index into the materials.
    float32 Linear[9];      ///< Column-major 3x3 linear part (X, Y, Z columns).
    float32 Translation[3]; ///< World translation.
};
/// Number of float32 components in ObjectRecord::Linear plus Translation.
inline constexpr usize OBJECT_TRANSFORM_COMPONENTS = 12;

static_assert(sizeof(float32) == 4 && sizeof(uint32) == 4);
static_assert(std::is_trivially_copyable_v<Header> && std::is_standard_layout_v<Header>);
static_assert(sizeof(Header) == 68 && offsetof(Header, Version) == 8 && offsetof(Header, Eye) == 28 &&
              offsetof(Header, VerticalFov) == 64);
static_assert(sizeof(TextureRecord) == 12);
static_assert(sizeof(MaterialRecordV1) == 24 && offsetof(MaterialRecordV1, Emission) == 12);
static_assert(sizeof(MaterialRecordV2) == 28 && offsetof(MaterialRecordV2, Metallic) == 8);
static_assert(sizeof(MeshRecord) == 8);
static_assert(sizeof(VertexRecord) == 32 && sizeof(VertexRecord) == VERTEX_COMPONENTS * sizeof(float32) &&
              offsetof(VertexRecord, Normal) == 12 && offsetof(VertexRecord, Uv) == 24);
static_assert(sizeof(ObjectRecord) == 56 && offsetof(ObjectRecord, Translation) == 44 &&
              sizeof(ObjectRecord) == 2 * sizeof(uint32) + OBJECT_TRANSFORM_COMPONENTS * sizeof(float32));
static_assert(MAGIC.size() == sizeof(Header::Magic));
} // namespace ludus::content::scene_format
