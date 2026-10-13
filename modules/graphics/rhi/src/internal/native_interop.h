#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/native_interop.h>

namespace ludus::graphics::rhi::internal
{
struct NativeTexturePublication final
{
    foundation::usize Lease = 0;
    foundation::usize Texture = 0;
};
bool NativeInteropBusy() noexcept;
bool NativeInteropPinned() noexcept;
bool DeferNativeInteropShutdown() noexcept;
void FinishNativeInteropShutdown() noexcept;
void ReleaseNativeDeviceSource() noexcept;
DeviceStatus PrepareNativeDeviceSource(const NativeDeviceDescription&) noexcept;
void NativeInteropShutdownFinished() noexcept;
const NativeDeviceDescription* NativeDeviceRequest() noexcept;
} // namespace ludus::graphics::rhi::internal
namespace ludus::graphics::rhi::backend
{
bool NativeInteropSupported() noexcept;
RasterStatus NativeDeviceSnapshot(NativeDeviceInfo&) noexcept;
RasterStatus NativeTexturePublish(const internal::NativeTexturePublication&, NativeTextureInfo&) noexcept;
RasterStatus NativeTextureRelease(foundation::usize) noexcept;
RasterStatus NativeTexturePoll(foundation::usize, bool) noexcept;
void NativeTextureDestroy(foundation::usize) noexcept;
void NativeInteropReset() noexcept;
void NativeInteropDetachSurface() noexcept;
} // namespace ludus::graphics::rhi::backend
