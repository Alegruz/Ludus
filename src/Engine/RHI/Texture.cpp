#include <Ludus/Engine/RHI/Texture.h>

namespace ludus::rhi
{
    Texture::Texture(const CreateInfo& createInfo) noexcept
        : mWidth(createInfo.Width)
        , mHeight(createInfo.Height)
        , mFormat(createInfo.Format)
    {
        // Vulkan texture allocation is not wired yet.
        LUDUS_ASSERT_MSG(false, "Vulkan Texture not implemented yet.");
    }
}   // namespace ludus::rhi
