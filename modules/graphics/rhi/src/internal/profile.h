#pragma once

#include <ludus/graphics/rhi/profile.h>

namespace ludus::graphics::rhi::internal
{
bool ValidProfileRequirements(const DeviceProfileRequirements&) noexcept;
bool NeedsRaster(const DeviceProfileRequirements&) noexcept;
bool NeedsCompute(const DeviceProfileRequirements&) noexcept;
ProfileDiagnostic CheckProfile(const DeviceProfileRequirements&, const DeviceProfile&) noexcept;
DeviceProfile QueryDeviceProfile() noexcept;
DeviceProfile CurrentDeviceProfile() noexcept;
// Guaranteed normalized formats and the enabled L4 HDR attachment profile.
FormatCapabilities PortableFormatLimits(RasterFormat, const RasterCapabilities&) noexcept;
} // namespace ludus::graphics::rhi::internal
