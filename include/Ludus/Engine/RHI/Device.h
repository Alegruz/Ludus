#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/SmartPtr.h>

#include <Ludus/Engine/RHI/AdapterChildObject.h>
#include <Ludus/Engine/RHI/CommandQueue.h>

namespace ludus::rhi
{
    class Adapter;

    struct DeviceMemberVariables
    {
    public:
        ~DeviceMemberVariables() = default;

    public:
        core::HashMap<core::String, core::UniquePtr<CommandQueue>> mCommandQueues;
    };

    class Device final : public AdapterChildObject
    {
    public:
        static constexpr const char* MAIN_COMMAND_QUEUE_NAME = "MainCommandQueue";

    public:
        struct CreateInfo final
        {
        };

    public:
        explicit Device(const Adapter& rhiAdapter) noexcept;
        LUDUS_INLINE ~Device() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

        [[nodiscard]] LUDUS_INLINE core::UniquePtr<CommandQueue>& CreateCommandQueue(const CommandQueue::CreateInfo& createInfo) noexcept { return createCommandQueue(createInfo); }

    private:
        [[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept;
        void shutdown() noexcept;
        [[nodiscard]] core::UniquePtr<CommandQueue>& createCommandQueue(const CommandQueue::CreateInfo& createInfo) noexcept;

    private:
        core::UniquePtr<DeviceMemberVariables> mMemberVariables;
    };
}   // namespace ludus::rhi
