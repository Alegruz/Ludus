#pragma once

#include <Ludus/Engine/Core/SmartPtr.h>
#include <Ludus/Engine/Core/ProjectInfo.h>

#include <Ludus/Engine/Platform/Window.h>

#include <Ludus/Engine/RHI/Common.h>

namespace ludus::rhi
{
    class Instance;
}

namespace ludus::renderer
{
    class Renderer final
    {
    public:
        struct CreateInfo final
        {
            uint32_t Width = 800;
            uint32_t Height = 600;
            rhi::TextureFormat Format = rhi::TextureFormat::DEFAULT;

            core::ProjectInfo ApplicationInfo{};
            core::ProjectInfo EngineInfo{};
            const platform::Window<platform::CURRENT_PLATFORM_TYPE>* Window = nullptr;
        };

    public:
        Renderer();
        ~Renderer();

        bool Initialize(const CreateInfo& createInfo = {}) noexcept;
        void RenderFrame();

        [[nodiscard]] LUDUS_INLINE const rhi::Instance& GetRhiInstance() const noexcept { return *mInstance; }

    private:
        core::UniquePtr<rhi::Instance> mInstance;
    };
}   // namespace ludus::renderer