#pragma once

#include <Ludus/Engine/Renderer/Renderer.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/Instance.h>

namespace ludus::renderer
{
    template<rhi::GraphicsApi GRAPHICS_API>
    Renderer<GRAPHICS_API>::Renderer(const CreateInfo& createInfo)
        : mBackBufferTexture()
        , mInstance(core::MakeUnique<rhi::Instance<GRAPHICS_API>>())
    {
        // TODO: CPU renderer uses a simple texture as back buffer for now, but this should all be abstracted into the swap chain abstraction
        if constexpr (GRAPHICS_API == rhi::GraphicsApi::CPU)
        {
            mBackBufferTexture = core::MakeUnique<rhi::Texture<GRAPHICS_API>>(createInfo.Width, createInfo.Height, createInfo.Format);
        }
    }

    template<rhi::GraphicsApi GRAPHICS_API>
    void Renderer<GRAPHICS_API>::RenderFrame()
    {
        if constexpr (GRAPHICS_API == rhi::GraphicsApi::CPU)
        {
            // CPU rendering logic (placeholder)
            core::DynamicArray<uint8_t>& data = mBackBufferTexture->GetData();
            // Simple example: fill the texture with a solid color (e.g., opaque red)
            const uint32_t width = mBackBufferTexture->GetWidth();
            const uint32_t height = mBackBufferTexture->GetHeight();
            const uint32_t bytesPerPixel = rhi::GetBytesPerPixel(mBackBufferTexture->GetFormat());
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t index = (y * width + x) * bytesPerPixel;
                    switch(mBackBufferTexture->GetFormat())
                    {
                        case rhi::TextureFormat::RGBA8_UNORM:
                            data[index + 0] = 255; // R
                            data[index + 1] = 0;   // G
                            data[index + 2] = 0;   // B
                            data[index + 3] = 255; // A
                            break;
                        case rhi::TextureFormat::BGRA8_UNORM:
                            data[index + 0] = 0;   // B
                            data[index + 1] = 0;   // G
                            data[index + 2] = 255; // R
                            data[index + 3] = 255; // A
                            break;
                        // Handle other formats as needed
                        default:
                            LUDUS_ASSERT_MSG(false, "Unsupported texture format in CPU renderer.");
                            break;
                    }
                }
            }
        }
        else
        {
            // Other API rendering logic would go here
            LUDUS_ASSERT_MSG(false, "RenderFrame not implemented for this Graphics API yet.");
        }
    }
}   // namespace ludus::renderer