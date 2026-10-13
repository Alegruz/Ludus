#pragma once
#include <ludus/graphics/rhi/raster.h>
namespace ludus::textures::normal {
inline constexpr foundation::uint8 BYTES[] = {255,128,128,255,128,255,128,255,0,128,128,255,128,0,128,255,180,180,232,255};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,8},{16,4}};
inline graphics::rhi::TextureDescription Description() noexcept { return {2,2,graphics::rhi::RasterFormat::Rgba8Unorm,false,2,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
}
