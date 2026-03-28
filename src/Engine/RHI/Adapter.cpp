#include <Ludus/Engine/RHI/Adapter.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

namespace ludus::rhi
{
    Adapter::Adapter(const Instance& rhiInstance) noexcept
        : InstanceChildObject(rhiInstance)
        , mMemberVariables(core::MakeUnique<AdapterMemberVariables>(*this))
    {
    }
    
    bool Adapter::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Adapter.");
            return false;
        }

        Device::CreateInfo deviceCreateInfo = {};
        if( createDevice(deviceCreateInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI Device during Adapter initialization.");
            return false;
        }

        return true;
    }

    LUDUS_INLINE bool Adapter::createDevice(const Device::CreateInfo& createInfo) noexcept
    {
        if( createDeviceImpl(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI Device.");
            return false;
        }

        return mMemberVariables->RhiDevice.Initialize(createInfo);
    }

    Adapter& Adapter::operator=(Adapter&& other) noexcept
    {
        if(this != &other)
        {
            mMemberVariables = std::move(other.mMemberVariables);
        }
        return *this;
    }

    bool Adapter::initialize([[maybe_unused]] const CreateInfo& createInfo) noexcept
    {
        // Vulkan RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter is not implemented yet.");

        return true;
    }

    void Adapter::shutdown() noexcept
    {
        // Vulkan RHI adapter shutdown logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter is not implemented yet.");
    }

    bool Adapter::createDeviceImpl([[maybe_unused]] const Device::CreateInfo& createInfo) noexcept
    {
        // Vulkan RHI device creation logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Device creation is not implemented yet.");
        
        return true;
    }
}   // namespace ludus::rhi
