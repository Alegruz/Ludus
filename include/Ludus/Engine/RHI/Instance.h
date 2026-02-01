#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>

namespace ludus::rhi
{
    struct InstanceMemberVariablesBase
    {
    public:
        virtual ~InstanceMemberVariablesBase() = default;
    };

    template<GraphicsApi GRAPHICS_API>
    class Instance final
    {
    public:
        explicit Instance() requires(GRAPHICS_API == GraphicsApi::CPU);
        explicit Instance() requires(GRAPHICS_API == GraphicsApi::VULKAN);
        Instance() = default;
        ~Instance() = default;

        LUDUS_INLINE bool Initialize() noexcept { return initialize(); }

    private:
        bool initialize() noexcept requires(GRAPHICS_API == GraphicsApi::CPU);
        bool initialize() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN);
        bool initialize() noexcept = delete;  // Unsupported Graphics API

    private:
        core::UniquePtr<InstanceMemberVariablesBase> mMemberVariables;
    };
}   // namespace ludus::rhi