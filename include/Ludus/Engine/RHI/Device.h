#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/Container/HashMap.h>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/SmartPtr.h>

#include <Ludus/Engine/RHI/AdapterChildObject.h>
#include <Ludus/Engine/RHI/CommandQueue.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Adapter;

    template<GraphicsApi GRAPHICS_API>
    struct DeviceMemberVariablesBase
    {
    public:
        virtual ~DeviceMemberVariablesBase() = default;

    public:
        core::HashMap<core::String, core::UniquePtr<CommandQueue<GRAPHICS_API>>> mCommandQueues;
    };

    template<GraphicsApi GRAPHICS_API>
    class Device final : public AdapterChildObject<GRAPHICS_API>
    {
    public:
        static constexpr const char* MAIN_COMMAND_QUEUE_NAME = "MainCommandQueue";

    public:
        struct CreateInfo final
        {
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit Device(const Adapter<GRAPHICS_API>& rhiAdapter) noexcept);
        LUDUS_INLINE ~Device() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

        [[nodiscard]] LUDUS_INLINE core::UniquePtr<CommandQueue<GRAPHICS_API>>& CreateCommandQueue(const CommandQueue<GRAPHICS_API>::CreateInfo& createInfo) noexcept { return createCommandQueue(createInfo); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] core::UniquePtr<CommandQueue<GRAPHICS_API>>& createCommandQueue(const CommandQueue<GRAPHICS_API>::CreateInfo& createInfo) noexcept);

    private:
        core::UniquePtr<DeviceMemberVariablesBase<GRAPHICS_API>> mMemberVariables;
    };

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Device<GRAPHICS_API>::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Device.");
            return false;
        }

        return true;
    }
}   // namespace ludus::rhi