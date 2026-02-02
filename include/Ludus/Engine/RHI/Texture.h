#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/Container/Array.hpp>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Texture final
    {
    public:
        struct CreateInfo final
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            TextureFormat Format = TextureFormat::DEFAULT;
        };

    public:
        explicit Texture(const CreateInfo& createInfo) requires(GRAPHICS_API == GraphicsApi::CPU);
        explicit Texture(const CreateInfo& createInfo) requires(GRAPHICS_API == GraphicsApi::VULKAN);
        explicit Texture(const CreateInfo& createInfo) requires(GRAPHICS_API == GraphicsApi::D3D12);
        Texture(const Texture&) = default;
        Texture& operator=(const Texture&) = default;
        Texture(Texture&&) noexcept = default;
        Texture& operator=(Texture&&) noexcept = default;
        ~Texture() = default;

        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetWidth() const noexcept { return mWidth; }
        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetHeight() const noexcept { return mHeight; }
        [[nodiscard]] LUDUS_INLINE constexpr TextureFormat GetFormat() const noexcept { return mFormat; }
        [[nodiscard]] LUDUS_INLINE constexpr const core::DynamicArray<uint8_t>& GetData() const noexcept requires(GRAPHICS_API == GraphicsApi::CPU) { return mData; }
        [[nodiscard]] LUDUS_INLINE constexpr core::DynamicArray<uint8_t>& GetData() noexcept requires(GRAPHICS_API == GraphicsApi::CPU) { return mData; }
        
    private:
        uint32_t mWidth;
        uint32_t mHeight;
        TextureFormat mFormat;
        [[no_unique_address]] std::conditional_t<GRAPHICS_API == GraphicsApi::CPU, core::DynamicArray<uint8_t>, std::monostate> mData;
    };
}   // namespace ludus::rhi
