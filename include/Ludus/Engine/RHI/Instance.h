#pragma once

#include <Ludus/Engine/RHI/Common.h>

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/Container/String.h>
#include <Ludus/Engine/Core/ProjectInfo.h>

#include <Ludus/Engine/Platform/Window.h>

namespace ludus::rhi
{
    struct InstanceMemberVariablesBase
    {
    public:
        virtual ~InstanceMemberVariablesBase() = default;
    };

#define LUDUS_DECLARATATION_BY_GRAPHICS_API(FUNCTION_SIGNATURE) \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::CPU); \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::VULKAN); \
    FUNCTION_SIGNATURE requires (GRAPHICS_API == GraphicsApi::D3D12); \
    FUNCTION_SIGNATURE = delete

    template<GraphicsApi GRAPHICS_API>
    class Instance final
    {
    public:
        struct CreateInfo final
        {
            core::ProjectInfo ApplicationInfo;
            core::ProjectInfo EngineInfo;
            const platform::Window<platform::CURRENT_PLATFORM_TYPE>& Window;
        };

    public:
        LUDUS_DECLARATATION_BY_GRAPHICS_API(explicit Instance() noexcept);
        LUDUS_INLINE ~Instance() noexcept { Shutdown(); }

        [[nodiscard]] LUDUS_INLINE bool Initialize(const CreateInfo& createInfo) noexcept { return initialize(createInfo); }
        LUDUS_INLINE void Shutdown() noexcept { shutdown(); }

    private:
        LUDUS_DECLARATATION_BY_GRAPHICS_API([[nodiscard]] bool initialize(const CreateInfo& createInfo) noexcept);
        LUDUS_DECLARATATION_BY_GRAPHICS_API(void shutdown() noexcept);

    private:
        core::UniquePtr<InstanceMemberVariablesBase> mMemberVariables;
    };
}   // namespace ludus::rhi