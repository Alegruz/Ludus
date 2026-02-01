#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/ProjectInfo.h>

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
        struct CreateInfo final
        {
            core::ProjectInfo ApplicationInfo;
            core::ProjectInfo EngineInfo;
        };

    public:
        explicit Instance() requires(GRAPHICS_API == GraphicsApi::CPU);
        explicit Instance() requires(GRAPHICS_API == GraphicsApi::VULKAN);
        Instance() = default;
        ~Instance() = default;

        LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }

    private:
        bool initialize(const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::CPU);
        bool initialize(const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN);
        bool initialize(const CreateInfo& createInfo) noexcept = delete;  // Unsupported Graphics API

    private:
        core::UniquePtr<InstanceMemberVariablesBase> mMemberVariables;
    };
}   // namespace ludus::rhi