#include <Ludus/Engine/RHI/Texture.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    Texture<GRAPHICS_API>::Texture(const CreateInfo& createInfo)
        requires(GRAPHICS_API == GraphicsApi::D3D12)
        : mWidth(createInfo.Width)
        , mHeight(createInfo.Height)
        , mFormat(createInfo.Format)
    {
        // D3D12 texture allocation is not wired yet.
        LUDUS_ASSERT_MSG(false, "D3D12 Texture not implemented yet.");
    }

    // Explicitly instantiate the D3D12 texture so the symbol is emitted from the shared library
    template class Texture<GraphicsApi::D3D12>;
}   // namespace ludus::rhi
