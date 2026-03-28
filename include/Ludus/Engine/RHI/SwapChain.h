#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/PlatformDetection.h>
#include <Ludus/Engine/Core/SmartPtr.h>

#include <Ludus/Engine/RHI/InstanceChildObject.h>

namespace ludus::platform
{
    template<core::PlatformType PLATFORM_TYPE>
    class Window;
}

namespace ludus::rhi
{
    struct SwapChainMemberVariablesBase
    {
    public:
        virtual ~SwapChainMemberVariablesBase() = default;
    };

    template<GraphicsApi GRAPHICS_API>
    class SwapChain final : public InstanceChildObject<GRAPHICS_API>
    {
    public:
        friend class Instance<GRAPHICS_API>;

    public:
        struct CreateInfo final
        {
            const platform::Window<core::CURRENT_PLATFORM_TYPE>& Window;
            uint32_t BufferCount = 3;
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit SwapChain(const Instance<GRAPHICS_API>& rhiInstance) noexcept);
        LUDUS_INLINE ~SwapChain() noexcept { Shutdown(); }

        [[nodiscard]] LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);

    private:
        core::UniquePtr<SwapChainMemberVariablesBase> mMemberVariables;
    };
}   // namespace ludus::rhi