#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_CPU)
#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{   
    struct InstanceMemberVariablesCpu final : public InstanceMemberVariablesBase
    {
    };

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() requires(GRAPHICS_API == GraphicsApi::CPU)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesCpu>())
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initialize() noexcept requires(GRAPHICS_API == GraphicsApi::CPU)
    {
        // CPU RHI instance initialization logic (if any) goes here.

        return true;
    }

    template class Instance<GraphicsApi::CPU>;
}   // namespace ludus::rhi
#endif  // defined(LUDUS_GRAPHICS_CPU)