#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <volk.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : mInstance(VK_NULL_HANDLE)
    {
        VkResult vr = volkInitialize();
        LUDUS_ASSERT_MSG(vr == VK_SUCCESS, "Failed to initialize Volk library.");
    }

    template class Instance<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)