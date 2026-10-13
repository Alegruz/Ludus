#pragma once
#include <ludus/graphics/rhi/raster.h>
namespace ludus::textures::mask {
inline constexpr foundation::uint8 BYTES[] = {255,255,255,0,0,128,200,30,160,121,0,0,141,0,0,0};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,4},{8,4},{12,4}};
inline graphics::rhi::TextureDescription Description() noexcept { return {4,2,graphics::rhi::RasterFormat::R8Unorm,false,3,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
}
