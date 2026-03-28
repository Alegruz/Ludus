#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/Device.h>
#include <Ludus/Engine/RHI/InstanceChildObject.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Adapter;

    template<GraphicsApi GRAPHICS_API>
    struct AdapterMemberVariablesBase
    {
    public:
        LUDUS_INLINE explicit AdapterMemberVariablesBase(const Adapter<GRAPHICS_API>& rhiAdapter) noexcept
            : RhiDevice(rhiAdapter)
        {
        }
        virtual ~AdapterMemberVariablesBase() = default;

    public:
        Device<GRAPHICS_API> RhiDevice;
    };

    template<GraphicsApi GRAPHICS_API>
    class Adapter final : public InstanceChildObject<GRAPHICS_API>
    {
    public:
        friend class Instance<GRAPHICS_API>;

    public:
        struct CreateInfo final
        {
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit Adapter(const Instance<GRAPHICS_API>& rhiInstance) noexcept);
        Adapter(Adapter&& other) noexcept = default;
        Adapter& operator=(Adapter&& other) noexcept;
        LUDUS_INLINE ~Adapter() noexcept { Shutdown(); }

        [[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool createDeviceImpl(const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept);
        [[nodiscard]] bool createDevice(const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept;

    private:
        core::UniquePtr<AdapterMemberVariablesBase<GRAPHICS_API>> mMemberVariables;
    };

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Adapter<GRAPHICS_API>::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Adapter.");
            return false;
        }

        typename Device<GRAPHICS_API>::CreateInfo deviceCreateInfo = {};
        if( createDevice(deviceCreateInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI Device during Adapter initialization.");
            return false;
        }

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE bool Adapter<GRAPHICS_API>::createDevice(const typename Device<GRAPHICS_API>::CreateInfo& createInfo) noexcept
    {
        if( createDeviceImpl(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI Device.");
            return false;
        }

        return mMemberVariables->RhiDevice.Initialize(createInfo);
    }

    template<GraphicsApi GRAPHICS_API>
    LUDUS_INLINE Adapter<GRAPHICS_API>& Adapter<GRAPHICS_API>::operator=(Adapter&& other) noexcept
    {
        if(this != &other)
        {
            mMemberVariables = std::move(other.mMemberVariables);
        }
        return *this;
    }
}   // namespace ludus::rhi