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
// Guaranteed normalized-format operations intersected with the implemented profile.
FormatCapabilities PortableFormatLimits(RasterFormat, const RasterCapabilities&) noexcept;
} // namespace ludus::graphics::rhi::internal
