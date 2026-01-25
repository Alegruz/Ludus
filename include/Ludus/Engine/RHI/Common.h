#pragma once

#include <Ludus/Engine/Core/Common.h>

#include <Ludus/Engine/Core/Assert.h>

namespace ludus::rhi
{
    enum class GraphicsApi : uint8_t
    {
        CPU,
        VULKAN,
        DIRECT3D12,
        METAL,
        COUNT,

        DEFAULT = CPU,
    };

    enum class TextureFormat : uint8_t
    {
        RGBA8_UNORM,
        BGRA8_UNORM,
        RGBA16_FLOAT,
        RGBA32_FLOAT,
        DEPTH24_STENCIL8,
        COUNT,

        DEFAULT = RGBA8_UNORM,
    };

    namespace detail
    {
        inline constexpr uint32_t BYTES_PER_PIXEL[] = {
            4u,  // RGBA8_UNORM
            4u,  // BGRA8_UNORM
            8u,  // RGBA16_FLOAT
            16u, // RGBA32_FLOAT
            4u,  // DEPTH24_STENCIL8
        };
        
        LUDUS_STATIC_ASSERT_MSG(std::size(BYTES_PER_PIXEL) == static_cast<size_t>(TextureFormat::COUNT), "BYTES_PER_PIXEL array size must match TextureFormat::COUNT");
    }

    [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetBytesPerPixel(TextureFormat format) noexcept
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if(index < static_cast<uint32_t>(TextureFormat::COUNT)) [[likely]]
        {
            return detail::BYTES_PER_PIXEL[index];
        }
        LUDUS_ASSERT_MSG(false, "Invalid TextureFormat value");
        return 0u;
    }
    
#if defined(LUDUS_GRAPHICS_CPU)
    constexpr GraphicsApi CURRENT_GRAPHICS_API = GraphicsApi::CPU;
#elif defined(LUDUS_GRAPHICS_VULKAN)
    constexpr GraphicsApi CURRENT_GRAPHICS_API = GraphicsApi::VULKAN;
#elif defined(LUDUS_GRAPHICS_D3D12)
    constexpr GraphicsApi CURRENT_GRAPHICS_API = GraphicsApi::DIRECT3D12;
#elif defined(LUDUS_GRAPHICS_METAL)
    constexpr GraphicsApi CURRENT_GRAPHICS_API = GraphicsApi::METAL;
#else
#error "No graphics API defined. Please define one of LUDUS_GRAPHICS_CPU, LUDUS_GRAPHICS_VULKAN, LUDUS_GRAPHICS_D3D12, or LUDUS_GRAPHICS_METAL."
#endif  // defined(LUDUS_GRAPHICS_CPU)
}
