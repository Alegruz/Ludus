#pragma once

#include <ludus/foundation/base/types.h>

#include <string>

#if defined(LUDUS_BUILD_DEBUG) || defined(LUDUS_BUILD_DEVELOPMENT)
#define LUDUS_RHI_ENABLE_VALIDATION_LAYER
#endif

namespace ludus::graphics::rhi
{
struct ApplicationInfo
{
    std::string Name;
    ludus::foundation::uint32 Version = 0;
};

// Loads Vulkan entry points and creates an instance. Returns false on failure.
// Repeated calls succeed until Shutdown; serialize all lifecycle calls.
// Does not create a device or guarantee that a usable GPU is present.
[[nodiscard]] bool Initialize(const ApplicationInfo& appInfo) noexcept;
void Shutdown() noexcept;
} // namespace ludus::graphics::rhi
