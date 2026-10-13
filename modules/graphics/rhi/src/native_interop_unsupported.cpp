#include "internal/native_interop.h"

namespace ludus::graphics::rhi::backend
{
bool NativeInteropSupported() noexcept
{
    return false;
}
RasterStatus NativeDeviceSnapshot(NativeDeviceInfo&) noexcept
{
    return RasterStatus::Unsupported;
}
RasterStatus NativeTexturePublish(const internal::NativeTexturePublication&, NativeTextureInfo&) noexcept
{
    return RasterStatus::Unsupported;
}
RasterStatus NativeTextureRelease(foundation::usize) noexcept
{
    return RasterStatus::Unsupported;
}
RasterStatus NativeTexturePoll(foundation::usize, bool) noexcept
{
    return RasterStatus::Unsupported;
}
void NativeTextureDestroy(foundation::usize) noexcept {}
void NativeInteropDetachSurface() noexcept {}
void NativeInteropReset() noexcept {}
} // namespace ludus::graphics::rhi::backend
