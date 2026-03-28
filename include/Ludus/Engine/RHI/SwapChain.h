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
    struct SwapChainMemberVariables
    {
    public:
        ~SwapChainMemberVariables() = default;
    };

    class SwapChain final : public InstanceChildObject
    {
    public:
        friend class Instance;

    public:
        struct CreateInfo final
        {
            const platform::Window<core::CURRENT_PLATFORM_TYPE>& Window;
            uint32_t BufferCount = 3;
        };

    public:
        explicit SwapChain(const Instance& rhiInstance) noexcept;
        LUDUS_INLINE ~SwapChain() noexcept { Shutdown(); }

        [[nodiscard]] LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        [[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept;
        void shutdown() noexcept;

    private:
        core::UniquePtr<SwapChainMemberVariables> mMemberVariables;
    };
}   // namespace ludus::rhi
