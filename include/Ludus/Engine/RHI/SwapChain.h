#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/PlatformDetection.h>
#include <Ludus/Engine/Core/SmartPtr.h>

namespace ludus::platform
{
    template<core::PlatformType PLATFORM_TYPE>
    class Window;
}

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Instance;

    struct SwapChainMemberVariablesBase
    {
    public:
        virtual ~SwapChainMemberVariablesBase() = default;
    };

#define LUDUS_DECLARATATION_BY_GRAPHICS_API(FUNCTION_SIGNATURE) \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::CPU); \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::VULKAN); \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::D3D12); \
    FUNCTION_SIGNATURE = delete

    template<GraphicsApi GRAPHICS_API>
    class SwapChain final
    {
    public:
        friend class Instance<GRAPHICS_API>;

    public:
        struct CreateInfo final
        {
            Instance<GRAPHICS_API>& RhiInstance;
            const platform::Window<core::CURRENT_PLATFORM_TYPE>& Window;
            uint32_t BufferCount = 3;
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit SwapChain() noexcept);
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