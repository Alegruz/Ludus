#pragma once

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/RHI/Common.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Texture;

    template<GraphicsApi GRAPHICS_API>
    class Instance;
}

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

            rhi::Instance<GRAPHICS_API>::CreateInfo RhiInstanceCreateInfo{};
        };

    public:
        Renderer();
        ~Renderer() = default;

        bool Initialize(const CreateInfo& createInfo = {}) noexcept;
        void RenderFrame();

        [[nodiscard]] LUDUS_INLINE constexpr const rhi::Texture<GRAPHICS_API>& GetBackBufferTexture() const noexcept requires(GRAPHICS_API == rhi::GraphicsApi::CPU) { return *mBackBufferTexture; }
        [[nodiscard]] LUDUS_INLINE constexpr const rhi::Instance<GRAPHICS_API>& GetRhiInstance() const noexcept { return *mInstance; }

    private:
        [[no_unique_address]] std::conditional_t<GRAPHICS_API == rhi::GraphicsApi::CPU, core::UniquePtr<rhi::Texture<GRAPHICS_API>>, std::monostate> mBackBufferTexture;
        core::UniquePtr<rhi::Instance<GRAPHICS_API>> mInstance;
    };
}   // namespace ludus::renderer