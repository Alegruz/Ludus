#pragma once

namespace ludus::rhi
{
    class Adapter;

    class AdapterChildObject
    {
    public:
        friend class Adapter;

    public:
        LUDUS_INLINE explicit constexpr AdapterChildObject(const Adapter& rhiAdapter) noexcept : mRhiAdapter(rhiAdapter) {}
        LUDUS_INLINE ~AdapterChildObject() noexcept = default;
    protected:
        const Adapter& mRhiAdapter;
    };
}   // namespace ludus::rhi