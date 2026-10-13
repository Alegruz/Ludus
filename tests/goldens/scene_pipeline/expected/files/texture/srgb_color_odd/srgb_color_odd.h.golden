#pragma once
#include <ludus/graphics/rhi/raster.h>
namespace ludus::textures::srgb_color_odd {
inline constexpr foundation::uint8 BYTES[] = {0,37,74,111,148,185,222,3,40,77,114,151,188,225,6,43,80,117,154,191,228,9,46,83,120,157,194,231,12,49,86,123,160,197,234,15,52,89,126,163,200,237,18,55,92,129,166,203,240,21,58,95,132,169,206,243,24,61,98,135,133,126,142,116,109,125,150,130,121,125,146,123};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,20},{60,8},{68,4}};
inline graphics::rhi::TextureDescription Description() noexcept { return {5,3,graphics::rhi::RasterFormat::Rgba8Srgb,false,3,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
}
