#pragma once
#include "internal/raster.h"
namespace ludus::graphics::rhi::reference
{
inline bool ProfileAvailable = true;
inline RasterStatus Next = RasterStatus::Ready;
inline RasterStatus Submission = RasterStatus::Ready;
inline foundation::uint32 LastRequest = 0;
inline foundation::uint64 Submitted = 0;
inline foundation::uint64 Completed = 0;
inline foundation::usize Creates[8]{};
inline foundation::usize Destroys[8]{};
inline foundation::usize Draws = 0;
inline internal::RasterPacket Packet;
void Reset() noexcept;
} // namespace ludus::graphics::rhi::reference
