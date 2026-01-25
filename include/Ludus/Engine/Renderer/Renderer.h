#pragma once

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/RHI/Common.h>

namespace ludus::renderer
{
    template<rhi::GraphicsApi GRAPHICS_API>
    class Renderer final
    {
    public:
        struct CreateInfo final
        {
            uint32_t Width = 800;
            uint32_t Height = 600;
            rhi::TextureFormat Format = rhi::TextureFormat::DEFAULT;
        };

    public:
        Renderer(const CreateInfo& createInfo = {});
        ~Renderer() = default;

        void RenderFrame();

        [[nodiscard]] LUDUS_INLINE constexpr const rhi::Texture<GRAPHICS_API>& GetBackBufferTexture() const noexcept { return *mBackBufferTexture; }

    private:
        core::UniquePtr<class rhi::Texture<GRAPHICS_API>> mBackBufferTexture;
    };
}   // namespace ludus::renderer