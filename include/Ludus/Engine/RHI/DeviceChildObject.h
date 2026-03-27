#pragma once

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Device;

    template<GraphicsApi GRAPHICS_API>
    class DeviceChildObject
    {
    public:
        friend class Device<GRAPHICS_API>;

    public:
        LUDUS_INLINE explicit constexpr DeviceChildObject(const Device<GRAPHICS_API>& rhiDevice) noexcept : mRhiDevice(rhiDevice) {}
        LUDUS_INLINE ~DeviceChildObject() noexcept = default;
    protected:
        const Device<GRAPHICS_API>& mRhiDevice;
    };
}   // namespace ludus::rhi