#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/graphics/rhi/native_interop.h>

namespace native_fixture
{
inline constexpr ludus::foundation::uint32 VERTEX_STRIDE = 3 * sizeof(ludus::foundation::float32);
inline constexpr ludus::foundation::float32 TRIANGLE_POSITIONS[]{-1, -1, 0, 3, -1, 0, -1, 3, 0};

struct Foreign final
{
    ludus::graphics::rhi::NativeDeviceDescription Description;
    void* Context = nullptr;
};
bool CreateForeign(Foreign&) noexcept;
bool AllowProducer(Foreign&) noexcept;
bool RetireConsumers(Foreign&) noexcept;
void DestroyForeign(Foreign&) noexcept;
bool SubmitSample(Foreign&, const ludus::graphics::rhi::NativeTextureInfo&) noexcept;
bool ReadSample(Foreign&, ludus::foundation::uint8 (&)[64]) noexcept;
ludus::foundation::usize Retains(const Foreign&) noexcept;
ludus::foundation::usize Releases(const Foreign&) noexcept;
} // namespace native_fixture
