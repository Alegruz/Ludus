// TGA layout: Truevision Inc., "Truevision TGA File Format Specification",
// version 2.0 (1991). PFM layout: Paul Debevec's Portable Float Map notes,
// https://www.pauldebevec.com/Research/HDR/PFM/. PNG decoding uses stb_image
// by Sean Barrett (vendored in third_party/stb).

#include "image_reader.h"

#include "../pbrt/pbrt_tokenizer.h"

#include <ludus/foundation/math/scalar.hpp>

#include <bit>

extern "C" {
#include "stb_image.h"
#include "stb_image_config.h"
}

namespace ludus::content::scene_cook::importers
{
namespace
{
namespace math = foundation::math;
using foundation::core::float32;
using foundation::core::float64;
using foundation::core::uint16;

[[nodiscard]] bool HasExtension(std::string_view name, std::string_view extension) noexcept
{
    if (name.size() < extension.size())
    {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - extension.size());
    for (usize i = 0; i < extension.size(); ++i)
    {
        char value = tail[i];
        if (value >= 'A' && value <= 'Z')
        {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (value != extension[i])
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool AllocateImage(uint32 width, uint32 height, DecodedImage& outImage) noexcept
{
    outImage.Width = width;
    outImage.Height = height;
    return outImage.Texels.TryResize(usize{width} * height * 4) == true;
}

[[nodiscard]] uint16 ReadUint16(std::span<const uint8> data, usize at) noexcept
{
    return static_cast<uint16>(data[at] | (data[at + 1] << 8));
}

[[nodiscard]] ImageResult
DecodeTga(std::span<const uint8> data, DecodedImage& outImage, std::string_view& outMessage) noexcept
{
    if (data.size() < 18)
    {
        outMessage = "TGA header is truncated";
        return ImageResult::Invalid;
    }
    const uint8 idLength = data[0];
    const uint8 colourMapType = data[1];
    const uint8 imageType = data[2];
    const uint16 mapFirst = ReadUint16(data, 3);
    const uint16 mapLength = ReadUint16(data, 5);
    const uint8 mapDepth = data[7];
    const uint32 width = ReadUint16(data, 12);
    const uint32 height = ReadUint16(data, 14);
    const uint8 depth = data[16];
    const uint8 descriptor = data[17];
    const bool isRle = imageType >= 9;
    const uint8 baseType = isRle == true ? static_cast<uint8>(imageType - 8) : imageType;
    if ((baseType != 1 && baseType != 2 && baseType != 3) || width == 0 || height == 0)
    {
        outMessage = "unsupported TGA image type";
        return ImageResult::Invalid;
    }
    const bool isMapped = baseType == 1;
    if ((isMapped == true && (colourMapType != 1 || (depth != 8 && depth != 16) ||
                              (mapDepth != 15 && mapDepth != 16 && mapDepth != 24 && mapDepth != 32))) ||
        (baseType == 2 && depth != 24 && depth != 32 && depth != 16 && depth != 15) ||
        (baseType == 3 && depth != 8 && depth != 16))
    {
        outMessage = "unsupported TGA pixel depth";
        return ImageResult::Invalid;
    }
    usize at = 18 + idLength;
    const usize mapEntryBytes = (static_cast<usize>(mapDepth) + 7) / 8;
    const usize mapOffset = at;
    if (colourMapType == 1)
    {
        at += mapEntryBytes * mapLength;
    }
    if (at > data.size())
    {
        outMessage = "TGA is truncated";
        return ImageResult::Invalid;
    }
    if (AllocateImage(width, height, outImage) == false)
    {
        return ImageResult::OutOfMemory;
    }
    const usize pixelBytes = (static_cast<usize>(depth) + 7) / 8;
    // Convert one stored pixel (or colour-map entry) of the given depth to RGBA.
    const auto convert = [](const uint8* source, usize bits, bool isGrey, uint8(&outRgba)[4]) noexcept {
        if (isGrey == true)
        {
            outRgba[0] = outRgba[1] = outRgba[2] = source[0];
            outRgba[3] = bits == 16 ? source[1] : uint8{255};
            return;
        }
        if (bits == 15 || bits == 16)
        {
            const uint16 packed = static_cast<uint16>(source[0] | (source[1] << 8));
            outRgba[0] = static_cast<uint8>(((packed >> 10) & 31) * 255 / 31);
            outRgba[1] = static_cast<uint8>(((packed >> 5) & 31) * 255 / 31);
            outRgba[2] = static_cast<uint8>((packed & 31) * 255 / 31);
            outRgba[3] = 255;
            return;
        }
        outRgba[0] = source[2];
        outRgba[1] = source[1];
        outRgba[2] = source[0];
        outRgba[3] = bits == 32 ? source[3] : uint8{255};
    };
    const auto decodePixel = [&](const uint8* source, uint8(&outRgba)[4]) noexcept -> bool {
        if (isMapped == false)
        {
            convert(source, depth, baseType == 3, outRgba);
            return true;
        }
        const usize index = depth == 8 ? source[0] : static_cast<usize>(source[0] | (source[1] << 8));
        if (index < mapFirst || index - mapFirst >= mapLength)
        {
            return false;
        }
        convert(data.data() + mapOffset + (index - mapFirst) * mapEntryBytes, mapDepth, false, outRgba);
        return true;
    };
    const usize count = usize{width} * height;
    usize written = 0;
    const bool isTopDown = (descriptor & 0x20) != 0;
    const auto store = [&](const uint8(&rgba)[4]) noexcept {
        const usize x = written % width;
        const usize row = written / width;
        const usize y = isTopDown == true ? row : height - 1 - row;
        uint8* target = outImage.Texels.GetData() + (y * width + x) * 4;
        for (usize k = 0; k < 4; ++k)
        {
            target[k] = rgba[k];
        }
        ++written;
    };
    while (written < count)
    {
        usize run = 1;
        bool isRepeated = false;
        if (isRle == true)
        {
            if (at >= data.size())
            {
                break;
            }
            const uint8 header = data[at++];
            run = static_cast<usize>(header & 0x7f) + 1;
            isRepeated = (header & 0x80) != 0;
        }
        for (usize i = 0; i < run && written < count; ++i)
        {
            if (i == 0 || isRepeated == false)
            {
                if (at + pixelBytes > data.size())
                {
                    outMessage = "TGA pixel data is truncated";
                    return ImageResult::Invalid;
                }
            }
            uint8 rgba[4] = {0, 0, 0, 255};
            if (decodePixel(data.data() + at, rgba) == false)
            {
                outMessage = "TGA colour-map index out of range";
                return ImageResult::Invalid;
            }
            if (isRepeated == false)
            {
                at += pixelBytes;
            }
            store(rgba);
        }
        if (isRepeated == true)
        {
            at += pixelBytes;
        }
    }
    if (written != count)
    {
        outMessage = "TGA pixel data is truncated";
        return ImageResult::Invalid;
    }
    return ImageResult::Ok;
}

[[nodiscard]] ImageResult
DecodePng(std::span<const uint8> data, DecodedImage& outImage, std::string_view& outMessage) noexcept
{
    if (data.size() > 0x7fffffffU)
    {
        outMessage = "PNG is too large";
        return ImageResult::Invalid;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        outMessage = "PNG decoding failed";
        return ImageResult::Invalid;
    }
    ImageResult result = ImageResult::Ok;
    if (width <= 0 || height <= 0 || static_cast<uint32>(width) > MAX_IMAGE_DIMENSION ||
        static_cast<uint32>(height) > MAX_IMAGE_DIMENSION)
    {
        outMessage = "PNG dimensions out of range";
        result = ImageResult::Invalid;
    }
    else if (AllocateImage(static_cast<uint32>(width), static_cast<uint32>(height), outImage) == false)
    {
        result = ImageResult::OutOfMemory;
    }
    else
    {
        for (usize i = 0; i < outImage.Texels.GetSize(); ++i)
        {
            outImage.Texels[i] = pixels[i];
        }
    }
    stbi_image_free(pixels);
    return result;
}

[[nodiscard]] ImageResult
DecodePfm(std::span<const uint8> data, DecodedImage& outImage, std::string_view& outMessage) noexcept
{
    // Header: "PF" or "Pf", width height, scale (negative = little-endian), each
    // followed by whitespace; rows run bottom to top.
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size() < 256 ? data.size() : 256);
    std::string_view words[4];
    usize at = 0;
    for (std::string_view& word : words)
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t'))
        {
            ++at;
        }
        const usize start = at;
        while (at < text.size() && text[at] != ' ' && text[at] != '\n' && text[at] != '\r' && text[at] != '\t')
        {
            ++at;
        }
        word = text.substr(start, at - start);
    }
    ++at; // The single whitespace byte before the raster.
    float64 width = 0.0;
    float64 height = 0.0;
    float64 scale = 0.0;
    const bool isColour = words[0] == "PF";
    if ((isColour == false && words[0] != "Pf") || pbrt::ParseNumber(words[1], width) == false ||
        pbrt::ParseNumber(words[2], height) == false || pbrt::ParseNumber(words[3], scale) == false || width < 1.0 ||
        height < 1.0 || width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION || scale == 0.0)
    {
        outMessage = "malformed PFM header";
        return ImageResult::Invalid;
    }
    const uint32 columns = static_cast<uint32>(width);
    const uint32 rows = static_cast<uint32>(height);
    const usize channels = isColour == true ? 3 : 1;
    if (at > data.size() || (data.size() - at) / 4 / channels / columns < rows)
    {
        outMessage = "PFM raster is truncated";
        return ImageResult::Invalid;
    }
    if (AllocateImage(columns, rows, outImage) == false)
    {
        return ImageResult::OutOfMemory;
    }
    outImage.WasHighDynamicRange = true;
    const bool isLittleEndian = scale < 0.0;
    for (uint32 y = 0; y < rows; ++y)
    {
        for (uint32 x = 0; x < columns; ++x)
        {
            uint8* target = outImage.Texels.GetData() + ((usize{rows} - 1 - y) * columns + x) * 4;
            for (usize k = 0; k < 3; ++k)
            {
                const usize channel = channels == 3 ? k : 0;
                const usize offset = at + ((usize{y} * columns + x) * channels + channel) * 4;
                uint32 bits = 0;
                for (usize b = 0; b < 4; ++b)
                {
                    const usize shift = isLittleEndian == true ? b : 3 - b;
                    bits |= static_cast<uint32>(data[offset + b]) << (8 * shift);
                }
                float32 value = std::bit_cast<float32>(bits);
                value = math::IsFinite(value) == true ? math::Clamp(value, 0.0F, 1.0F) : 0.0F;
                target[k] =
                    static_cast<uint8>(math::Floor(static_cast<float64>(math::EncodeSrgb(value)) * 255.0 + 0.5));
            }
            target[3] = 255;
        }
    }
    return ImageResult::Ok;
}
} // namespace

ImageResult DecodeImage(std::string_view fileName,
                        std::span<const uint8> data,
                        DecodedImage& outImage,
                        std::string_view& outMessage) noexcept
{
    outImage.Width = 0;
    outImage.Height = 0;
    outImage.Texels.Clear();
    outImage.WasHighDynamicRange = false;
    static constexpr uint8 PNG_MAGIC[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    bool isPng = data.size() >= 8;
    for (usize i = 0; isPng == true && i < 8; ++i)
    {
        isPng = data[i] == PNG_MAGIC[i];
    }
    if (isPng == true)
    {
        return DecodePng(data, outImage, outMessage);
    }
    if (HasExtension(fileName, ".tga") == true)
    {
        return DecodeTga(data, outImage, outMessage);
    }
    if (HasExtension(fileName, ".pfm") == true)
    {
        return DecodePfm(data, outImage, outMessage);
    }
    outMessage = "no decoder for this image format in this build";
    return ImageResult::Unsupported;
}
} // namespace ludus::content::scene_cook::importers
