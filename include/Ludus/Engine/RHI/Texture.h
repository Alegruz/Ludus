#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/Container/Array.hpp>

namespace ludus::rhi
{
    template<GraphicsApi TApi>
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
        explicit Texture(const CreateInfo& createInfo) requires(TApi == GraphicsApi::CPU);
        ~Texture() = default;

        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetWidth() const noexcept { return mWidth; }
        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetHeight() const noexcept { return mHeight; }
        [[nodiscard]] LUDUS_INLINE constexpr TextureFormat GetFormat() const noexcept { return mFormat; }
        [[nodiscard]] LUDUS_INLINE constexpr const core::DynamicArray<uint8_t>& GetData() const noexcept requires(TApi == GraphicsApi::CPU) { return mData; }
        [[nodiscard]] LUDUS_INLINE constexpr core::DynamicArray<uint8_t>& GetData() noexcept requires(TApi == GraphicsApi::CPU) { return mData; }
        
    private:
        uint32_t mWidth;
        uint32_t mHeight;
        TextureFormat mFormat;
        [[no_unique_address]] std::conditional_t<TApi == GraphicsApi::CPU, core::DynamicArray<uint8_t>, std::monostate> mData;
    };
}   // namespace ludus::rhi