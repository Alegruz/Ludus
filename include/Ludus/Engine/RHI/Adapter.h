#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/Device.h>
#include <Ludus/Engine/RHI/InstanceChildObject.h>

namespace ludus::rhi
{
    class Adapter;

    struct AdapterMemberVariables
    {
    public:
        LUDUS_INLINE explicit AdapterMemberVariables(const Adapter& rhiAdapter) noexcept
            : RhiDevice(rhiAdapter)
        {
        }
        ~AdapterMemberVariables() = default;

    public:
        Device RhiDevice;
    };

    class Adapter final : public InstanceChildObject
    {
    public:
        friend class Instance;

    public:
        struct CreateInfo final
        {
        };

    public:
        explicit Adapter(const Instance& rhiInstance) noexcept;
        Adapter(Adapter&& other) noexcept = default;
        Adapter& operator=(Adapter&& other) noexcept;
        LUDUS_INLINE ~Adapter() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        [[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept;
        void shutdown() noexcept;
        [[nodiscard]] bool createDeviceImpl(const typename Device::CreateInfo& createInfo) noexcept;
        [[nodiscard]] bool createDevice(const typename Device::CreateInfo& createInfo) noexcept;

    private:
        core::UniquePtr<AdapterMemberVariables> mMemberVariables;
    };
}   // namespace ludus::rhi
