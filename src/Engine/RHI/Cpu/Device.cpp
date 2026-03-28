#include <Ludus/Engine/RHI/Device.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

namespace ludus::rhi
{   
    struct DeviceMemberVariablesCpu final : public DeviceMemberVariablesBase<GraphicsApi::CPU>
    {
    };

    template<GraphicsApi GRAPHICS_API>
    Device<GRAPHICS_API>::Device(const Adapter<GRAPHICS_API>& rhiAdapter) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
        : AdapterChildObject<GRAPHICS_API>(rhiAdapter)
        , mMemberVariables(core::MakeUnique<DeviceMemberVariablesCpu>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Device<GRAPHICS_API>::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI device initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Device is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Device<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI device shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI Device is not implemented yet.");
    }

    template<GraphicsApi GRAPHICS_API>
    core::UniquePtr<CommandQueue<GRAPHICS_API>>& Device<GRAPHICS_API>::createCommandQueue([[maybe_unused]] const CommandQueue<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI command queue creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "CPU RHI CommandQueue creation is not implemented yet.");

        // Placeholder return to satisfy the compiler; replace with actual implementation.
        static core::UniquePtr<CommandQueue<GRAPHICS_API>> dummyQueue;
        return dummyQueue;
    }

    template class Device<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)