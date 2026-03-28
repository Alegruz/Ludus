#include <Ludus/Engine/Renderer/Renderer.h>

#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <Ludus/Engine/RHI/Instance.h>

namespace ludus::renderer
{
    Renderer::Renderer()
        : mInstance(core::MakeUnique<rhi::Instance>())
    {
    }

    Renderer::~Renderer() = default;

    bool Renderer::Initialize(const CreateInfo& createInfo) noexcept
    {
        if(mInstance == nullptr)
        {
            LUDUS_ASSERT_MSG(false, "Renderer RHI instance is null.");
            return false;
        }

        if(createInfo.Window == nullptr)
        {
            LUDUS_ASSERT_MSG(false, "Renderer CreateInfo.Window is null.");
            return false;
        }

        const rhi::Instance::CreateInfo rhiCreateInfo
        {
            .ApplicationInfo = createInfo.ApplicationInfo,
            .EngineInfo = createInfo.EngineInfo,
            .Window = *createInfo.Window,
        };

        return mInstance->Initialize(rhiCreateInfo);
    }

    void Renderer::RenderFrame()
    {
        // Vulkan-only renderer scaffold: rendering work will be submitted here.
    }
}   // namespace ludus::renderer