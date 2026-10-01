#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/rhi.h>

namespace ludus::graphics::rhi::internal
{
// Numeric callback tokens never borrow application memory and never wrap.
bool Current(ludus::foundation::uint32 token) noexcept;
void Complete(ludus::foundation::uint32 token,
              StartupError error,
              ludus::foundation::uint32 maxTextureDimension) noexcept;
void Fail(ludus::foundation::uint32 token, StartupError error) noexcept;
} // namespace ludus::graphics::rhi::internal
