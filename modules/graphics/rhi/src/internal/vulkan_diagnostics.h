#pragma once

#include <ludus/foundation/base/assert_format.hpp>

#include <vulkan/vulkan_core.h>

namespace ludus::foundation::diagnostics::detail
{
// Logging already promotes VkResult to a signed integer. Give the assertion
// packer the same representation without admitting arbitrary types or making
// Foundation depend on Vulkan. Include before any formatted Vulkan assertions.
template <>
inline DiagnosticArg MakeDiagnosticArg<VkResult>(const VkResult& value) noexcept
{
    return MakeDiagnosticArg(static_cast<int32>(value));
}
} // namespace ludus::foundation::diagnostics::detail
