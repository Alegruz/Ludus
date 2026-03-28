#pragma once

namespace ludus::rhi
{
    class Instance;

    class InstanceChildObject
    {
    public:
        friend class Instance;

    public:
        LUDUS_INLINE explicit constexpr InstanceChildObject(const Instance& rhiInstance) noexcept : mRhiInstance(rhiInstance) {}
        LUDUS_INLINE ~InstanceChildObject() noexcept = default;

    protected:
        const Instance& mRhiInstance;
    };
}   // namespace ludus::rhi