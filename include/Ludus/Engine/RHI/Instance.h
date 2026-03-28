#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/ProjectInfo.h>

#include <Ludus/Engine/Platform/Window.h>

#include <Ludus/Engine/RHI/Adapter.h>
#include <Ludus/Engine/RHI/SwapChain.h>

namespace ludus::rhi
{
    class Instance;

    struct InstanceMemberVariables;

    struct InstanceMemberVariablesDeleter final
    {
        void operator()(InstanceMemberVariables* ptr) const noexcept;
    };

    class Instance final
    {
    public:
        struct CreateInfo final
        {
            core::ProjectInfo ApplicationInfo;
            core::ProjectInfo EngineInfo;
            const platform::Window<platform::CURRENT_PLATFORM_TYPE>& Window;
        };

    public:
        explicit Instance() noexcept;
        LUDUS_INLINE ~Instance() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        [[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept;
        [[nodiscard]] bool initializePostSwapChainInitialization(const CreateInfo& createInfo) noexcept;
        void shutdown() noexcept;
        [[nodiscard]] bool initializeAdaptersImpl() noexcept;
        [[nodiscard]] bool createSwapChainImpl(const SwapChain::CreateInfo& createInfo) noexcept;
        [[nodiscard]] bool initializeAdapters() noexcept;
        [[nodiscard]] bool createSwapChain(const SwapChain::CreateInfo& createInfo) noexcept;

    private:
        core::UniquePtr<InstanceMemberVariables, InstanceMemberVariablesDeleter> mMemberVariables;
    };
}   // namespace ludus::rhi
