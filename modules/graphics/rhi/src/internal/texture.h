#pragma once
// Thanks to Khronos, Vulkan Specification, "Copying Data Between Buffers and Images",
// https://docs.vulkan.org/spec/latest/chapters/copies.html#copies-buffers-images,
// and W3C GPU for the Web Working Group, WebGPU, "Copying texture data",
// https://www.w3.org/TR/webgpu/#copying-texture-data, for block-row and mip bounds.
// R9 keeps the encoded format and caller padding; it performs no conversion.
#include <ludus/graphics/rhi/raster.h>

namespace ludus::graphics::rhi::internal
{
inline bool TextureBinding(RasterBindingKind kind) noexcept
{
    return kind == RasterBindingKind::Texture2D || kind == RasterBindingKind::Texture2DArray ||
           kind == RasterBindingKind::TextureCube || kind == RasterBindingKind::Texture3D;
}
inline RasterBindingKind TextureBindingType(TextureDimension dimension) noexcept
{
    return dimension == TextureDimension::D2Array ? RasterBindingKind::Texture2DArray
           : dimension == TextureDimension::Cube  ? RasterBindingKind::TextureCube
           : dimension == TextureDimension::D3    ? RasterBindingKind::Texture3D
                                                  : RasterBindingKind::Texture2D;
}
inline bool CompressedTexture(RasterFormat format) noexcept
{
    return format == RasterFormat::Bc1RgbaUnorm || format == RasterFormat::Bc3RgbaUnorm;
}
inline foundation::uint32 TextureBlockWidth(RasterFormat format) noexcept
{
    return CompressedTexture(format) ? 4U : 1U;
}
inline foundation::usize TextureBlockBytes(RasterFormat format) noexcept
{
    return format == RasterFormat::Bc1RgbaUnorm   ? 8U
           : format == RasterFormat::Bc3RgbaUnorm ? 16U
           : format == RasterFormat::Rgba16Float  ? 8U
           : format == RasterFormat::Rg8Unorm     ? 2U
           : format == RasterFormat::R8Unorm      ? 1U
                                                  : 4U;
}
inline foundation::uint32 TextureMipExtent(foundation::uint32 extent, foundation::uint32 mip) noexcept
{
    return (extent >> mip) != 0 ? extent >> mip : 1U;
}
inline foundation::uint32 TextureSlices(const TextureDescription& info, foundation::uint32 mip) noexcept
{
    return info.Dimension == TextureDimension::D3 ? TextureMipExtent(info.DepthOrLayers, mip) : info.DepthOrLayers;
}
inline foundation::uint32 TextureBlockRows(RasterFormat format, foundation::uint32 height) noexcept
{
    const auto block = TextureBlockWidth(format);
    return (height + block - 1) / block;
}
} // namespace ludus::graphics::rhi::internal
