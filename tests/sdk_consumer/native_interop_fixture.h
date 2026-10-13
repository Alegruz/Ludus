#pragma once

#include <ludus/foundation/base/types.h>
#include <ludus/graphics/rhi/native_interop.h>

namespace native_fixture
{
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
