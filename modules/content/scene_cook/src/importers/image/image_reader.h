#pragma once

// Texture image decoding for the best-effort importers: TGA (including RLE and
// colour-mapped), PNG (vendored stb_image) and PFM. Output is RGBA8, rows top
// to bottom, with colour channels kept in the file's encoding.

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/containers/array.hpp>

#include <span>
#include <string_view>

namespace ludus::content::scene_cook::importers
{
using foundation::Array;
using foundation::core::uint32;
using foundation::core::uint8;
using foundation::core::usize;

/// Largest decoded image side.
inline constexpr uint32 MAX_IMAGE_DIMENSION = 16384;

/// Decoded RGBA8 image.
struct DecodedImage final
{
    uint32 Width = 0;
    uint32 Height = 0;
    /// Width * Height * 4 bytes, top row first.
    Array<uint8> Texels;
    /// True when the source stored linear (HDR) values that were tone-mapped and sRGB-encoded.
    bool WasHighDynamicRange = false;
};

/// Outcome of DecodeImage.
enum class ImageResult : uint8
{
    Ok,
    /// The container (for example EXR, TIFF) has no decoder in this build.
    Unsupported,
    Invalid,
    OutOfMemory,
};

/// Decode data, choosing the format from the file name extension (case-insensitive)
/// and the magic bytes.
[[nodiscard]] ImageResult DecodeImage(std::string_view fileName,
                                      std::span<const uint8> data,
                                      DecodedImage& outImage,
                                      std::string_view& outMessage) noexcept;
} // namespace ludus::content::scene_cook::importers
