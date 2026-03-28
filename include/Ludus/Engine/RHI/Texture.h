#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/Container/Array.hpp>

namespace ludus::rhi
{
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
        explicit Texture(const CreateInfo& createInfo) noexcept;
        Texture(const Texture&) = default;
        Texture& operator=(const Texture&) = default;
        Texture(Texture&&) noexcept = default;
        Texture& operator=(Texture&&) noexcept = default;
        ~Texture() = default;

        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetWidth() const noexcept { return mWidth; }
        [[nodiscard]] LUDUS_INLINE constexpr uint32_t GetHeight() const noexcept { return mHeight; }
        [[nodiscard]] LUDUS_INLINE constexpr TextureFormat GetFormat() const noexcept { return mFormat; }
        
    private:
        uint32_t mWidth;
        uint32_t mHeight;
        TextureFormat mFormat;
    };
}   // namespace ludus::rhi
