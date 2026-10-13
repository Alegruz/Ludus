#pragma once
#include <ludus/graphics/rhi/raster.h>
#include <ludus/graphics/renderer/lighting.hpp>
namespace ludus::textures::l4_bake {
inline constexpr foundation::uint8 BYTES[] = {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {{0,16}};
inline graphics::rhi::TextureDescription Description() noexcept { return {4,4,graphics::rhi::RasterFormat::Rgba8Unorm,false,1,true}; }
inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }
inline graphics::renderer::BakedLighting Bake(graphics::rhi::TextureHandle texture) noexcept { graphics::renderer::BakedLighting result; result.Texture=texture;result.Scale={1.0F,1.0F,1.0F};result.UvTransform={0.25F,0.25F,0.375F,0.375F};result.SourceId=2ULL;result.SceneRevision=1ULL;result.DependencyRevision=11ULL;result.DirectLightId=1ULL;result.Transform={{{1.0F,0.0F,0.0F},{0.0F,1.0F,0.0F},{0.0F,0.0F,1.0F}},{0.0F,0.45F,0.0F}};return result;}
}
