#pragma once

#include <Ludus/Engine/RHI/Common.h>

namespace ludus::rhi
{
    template<GraphicsApi GRAPHICS_API>
    class Instance final
    {
    public:
        explicit Instance() requires(GRAPHICS_API == GraphicsApi::VULKAN); // Existing constructor
        Instance() = default; // Added default constructor
        ~Instance() = default;

    private:
        [[no_unique_address]] std::conditional_t<GRAPHICS_API == GraphicsApi::VULKAN, VkInstance, std::monostate> mInstance;
    };
}   // namespace ludus::rhi