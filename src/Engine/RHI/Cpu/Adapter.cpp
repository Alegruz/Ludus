#include <Ludus/Engine/RHI/Adapter.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct AdapterMemberVariablesCpu final : public AdapterMemberVariablesBase<GraphicsApi::CPU>
    {
        LUDUS_INLINE explicit AdapterMemberVariablesCpu(const Adapter<GraphicsApi::CPU>& rhiAdapter) noexcept
            : AdapterMemberVariablesBase<GraphicsApi::CPU>(rhiAdapter)
        {
        }
    };

    template<GraphicsApi GRAPHICS_API>
    Adapter<GRAPHICS_API>::Adapter(const Instance<GRAPHICS_API>& rhiInstance) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
        : InstanceChildObject<GRAPHICS_API>(rhiInstance)
        , mMemberVariables(core::MakeUnique<AdapterMemberVariablesCpu>(*this))
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Adapter is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Adapter<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI adapter shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Adapter is not implemented yet.");
    }

    template<GraphicsApi GRAPHICS_API>
    bool Adapter<GRAPHICS_API>::createDeviceImpl([[maybe_unused]] const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI device creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Device creation is not implemented yet.");
        
        return true;
    }

    template class Adapter<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)