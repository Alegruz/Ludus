#pragma once

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Adapter;

    template<GraphicsApi GRAPHICS_API>
    class AdapterChildObject
    {
    public:
        friend class Adapter<GRAPHICS_API>;

    public:
        LUDUS_INLINE explicit constexpr AdapterChildObject(const Adapter<GRAPHICS_API>& rhiAdapter) noexcept : mRhiAdapter(rhiAdapter) {}
        LUDUS_INLINE ~AdapterChildObject() noexcept = default;
    protected:
        const Adapter<GRAPHICS_API>& mRhiAdapter;
    };
}   // namespace ludus::rhi