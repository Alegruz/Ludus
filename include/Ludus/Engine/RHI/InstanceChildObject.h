#pragma once

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Instance;

    template<GraphicsApi GRAPHICS_API>
    class InstanceChildObject
    {
    public:
        friend class Instance<GRAPHICS_API>;

    public:
        LUDUS_INLINE explicit constexpr InstanceChildObject(const Instance<GRAPHICS_API>& rhiInstance) noexcept : mRhiInstance(rhiInstance) {}
        LUDUS_INLINE ~InstanceChildObject() noexcept = default;

    protected:
        const Instance<GRAPHICS_API>& mRhiInstance;
    };
}   // namespace ludus::rhi