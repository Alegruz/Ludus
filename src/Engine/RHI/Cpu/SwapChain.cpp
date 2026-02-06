#include <Ludus/Engine/RHI/SwapChain.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct SwapChainMemberVariablesCpu final : public SwapChainMemberVariablesBase
    {
    };

    template<GraphicsApi GRAPHICS_API>
    SwapChain<GRAPHICS_API>::SwapChain(const Instance<GRAPHICS_API>& rhiInstance) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
        : InstanceChildObject<GRAPHICS_API>(rhiInstance)
        , mMemberVariables(core::MakeUnique<SwapChainMemberVariablesCpu>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool SwapChain<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI swap chain initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI SwapChain is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void SwapChain<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI swap chain shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI SwapChain is not implemented yet.");
    }

    template class SwapChain<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)