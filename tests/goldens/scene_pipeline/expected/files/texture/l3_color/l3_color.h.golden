#pragma once
#include <ludus/graphics/rhi/raster.h>
namespace ludus::textures::l3_color {
inline constexpr foundation::uint8 BYTES[] = {255,255,255,255,0,0,0,255,0,0,0,255,255,255,255,255,188,188,188,255};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,8},{16,4}};
inline graphics::rhi::TextureDescription Description() noexcept { return {2,2,graphics::rhi::RasterFormat::Rgba8Srgb,false,2,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
}
