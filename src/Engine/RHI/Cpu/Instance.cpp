#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct InstanceMemberVariablesCpu final : public InstanceMemberVariablesBase<GraphicsApi::CPU>
    {
        LUDUS_INLINE explicit InstanceMemberVariablesCpu(Instance<GraphicsApi::CPU>& rhiInstance)
            : InstanceMemberVariablesBase<GraphicsApi::CPU>(rhiInstance)
        {
        }
    };

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesCpu>(*this))
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI instance initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Instance is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initializePostSwapChainInitialization([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI post swap chain initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Instance post swap chain initialization is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Instance<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI instance shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Instance is not implemented yet.");
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initializeAdaptersImpl() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Adapter initialization is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::createSwapChainImpl([[maybe_unused]] const SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI swap chain creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI SwapChain creation is not implemented yet.");

        return true;
    }

    template class Instance<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)