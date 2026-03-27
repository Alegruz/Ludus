#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.h>

#include <Ludus/Engine/RHI/DeviceChildObject.h>

namespace ludus::rhi
{
    struct CommandQueueMemberVariablesBase
    {
    public:
        virtual ~CommandQueueMemberVariablesBase() = default;

    public:
        core::String Name;
    };

    template<GraphicsApi GRAPHICS_API>
    class CommandQueue final : public DeviceChildObject<GRAPHICS_API>
    {
    public:
        friend class Device<GRAPHICS_API>;

    public:
        struct CreateInfo final
        {
            core::String Name;
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit CommandQueue(const Device<GRAPHICS_API>& rhiDevice) noexcept);
        LUDUS_INLINE ~CommandQueue() noexcept { Shutdown(); }

        [[nodiscard]] LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);

    private:
        core::UniquePtr<CommandQueueMemberVariablesBase> mMemberVariables;
    };
}   // namespace ludus::rhi