#include <Ludus/Engine/RHI/Adapter.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct AdapterMemberVariablesVulkan final : public AdapterMemberVariablesBase<GraphicsApi::VULKAN>
    {
        LUDUS_INLINE explicit AdapterMemberVariablesVulkan(const Adapter<GraphicsApi::VULKAN>& rhiAdapter) noexcept
            : AdapterMemberVariablesBase<GraphicsApi::VULKAN>(rhiAdapter)
        {
        }
    };

    template<GraphicsApi GRAPHICS_API>
    Adapter<GRAPHICS_API>::Adapter(const Instance<GRAPHICS_API>& rhiInstance) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : InstanceChildObject<GRAPHICS_API>(rhiInstance)
        , mMemberVariables(core::MakeUnique<AdapterMemberVariablesVulkan>(*this))
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        // Vulkan RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Adapter<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        // Vulkan RHI adapter shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter is not implemented yet.");
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::createDeviceImpl([[maybe_unused]] const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        // Vulkan RHI device creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Device creation is not implemented yet.");
        
        return true;
    }

    template class Adapter<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_VULKAN)