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
    template<GraphicsApi GRAPHICS_API>
    struct InstanceMemberVariablesBase
    {
    public:
        LUDUS_INLINE explicit InstanceMemberVariablesBase(Instance<GRAPHICS_API>& rhiInstance)
            : SwapChain(rhiInstance)
        {
        }
        virtual ~InstanceMemberVariablesBase() = default;

    public:
        static constexpr const int32_t INVALID_ADAPTER_INDEX = -1;
        SwapChain<GRAPHICS_API> SwapChain;
        core::DynamicArray<Adapter<GRAPHICS_API>> Adapters;
        int32_t MainAdapterIndex = INVALID_ADAPTER_INDEX;
    };

    template<GraphicsApi GRAPHICS_API>
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
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit Instance() noexcept);
        LUDUS_INLINE ~Instance() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initializePostSwapChainInitialization(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initializeAdaptersImpl() noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool createSwapChainImpl(const typename SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept);
        [[nodiscard]] bool initializeAdapters() noexcept;
        [[nodiscard]] bool createSwapChain(const typename SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept;

    private:
        core::UniquePtr<InstanceMemberVariablesBase<GRAPHICS_API>> mMemberVariables;
    };

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Instance<GRAPHICS_API>::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Instance.");
            return false;
        }

        typename SwapChain<GRAPHICS_API>::CreateInfo swapChainCreateInfo =
        {
            .Window = createInfo.Window,
            .BufferCount = 3,
        };

        if( createSwapChain(swapChainCreateInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI SwapChain.");
            return false;
        }

        if( initializePostSwapChainInitialization(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to perform post swap chain initialization for RHI Instance.");
            return false;
        }

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Instance<GRAPHICS_API>::initializeAdapters() noexcept
    {
        if( this->initializeAdaptersImpl() == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Adapters.");
            return false;
        }
        typename Adapter<GRAPHICS_API>::CreateInfo adapterCreateInfo = {};
        return mMemberVariables->Adapters.GetBack().Initialize(adapterCreateInfo);
    }

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Instance<GRAPHICS_API>::createSwapChain(const typename SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept
    {
        if( this->createSwapChainImpl(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI SwapChain.");
            return false;
        }
        return mMemberVariables->SwapChain.Initialize(createInfo); 
    }
}   // namespace ludus::rhi