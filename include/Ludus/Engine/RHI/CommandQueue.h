#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.h>

#include <Ludus/Engine/RHI/DeviceChildObject.h>

namespace ludus::rhi
{
    struct CommandQueueMemberVariables
    {
    public:
        ~CommandQueueMemberVariables() = default;

    public:
        core::String Name;
    };

    class CommandQueue final : public DeviceChildObject
    {
    public:
        friend class Device;

    public:
        struct CreateInfo final
        {
            core::String Name;
        };

    public:
        explicit CommandQueue(const Device& rhiDevice) noexcept;
        LUDUS_INLINE ~CommandQueue() noexcept { Shutdown(); }

        [[nodiscard]] LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        [[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept;
        void shutdown() noexcept;

    private:
        core::UniquePtr<CommandQueueMemberVariables> mMemberVariables;
    };
}   // namespace ludus::rhi
