#include <Ludus/Engine/RHI/Texture.h>

namespace ludus::rhi
{
    template<GraphicsApi TApi>
    Texture<TApi>::Texture(const CreateInfo& createInfo)
        requires(TApi == GraphicsApi::VULKAN)
        : mWidth(createInfo.Width)
        , mHeight(createInfo.Height)
        , mFormat(createInfo.Format)
    {
        // Vulkan texture allocation is not wired yet.
        LUDUS_ASSERT_MSG(false, "Vulkan Texture not implemented yet.");
    }

    // Explicitly instantiate the Vulkan texture so the symbol is emitted from the shared library
    template class Texture<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi
