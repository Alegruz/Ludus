#pragma once
#include <ludus/graphics/rhi/raster.h>
namespace ludus::textures::rg_data {
inline constexpr foundation::uint8 BYTES[] = {0,255,17,34,51,68,0,0,85,102,119,136,153,170,0,0,71,128,0,0};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,8},{16,4}};
inline graphics::rhi::TextureDescription Description() noexcept { return {3,2,graphics::rhi::RasterFormat::Rg8Unorm,false,2,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
}
