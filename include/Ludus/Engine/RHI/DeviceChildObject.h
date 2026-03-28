#pragma once

namespace ludus::rhi
{
    class Device;

    class DeviceChildObject
    {
    public:
        friend class Device;

    public:
        LUDUS_INLINE explicit constexpr DeviceChildObject(const Device& rhiDevice) noexcept : mRhiDevice(rhiDevice) {}
        LUDUS_INLINE ~DeviceChildObject() noexcept = default;
    protected:
        const Device& mRhiDevice;
    };
}   // namespace ludus::rhi