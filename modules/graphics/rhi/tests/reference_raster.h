#pragma once
#include "internal/raster.h"
namespace ludus::graphics::rhi::reference
{
inline bool ProfileAvailable = true;
inline RasterStatus Next = RasterStatus::Ready;
inline RasterStatus Submission = RasterStatus::Ready;
inline foundation::uint32 LastRequest = 0;
inline foundation::uint32 LossOnCompletion = 0;
inline foundation::uint64 Submitted = 0;
inline foundation::uint64 Completed = 0;
inline foundation::usize Creates[8]{};
inline foundation::usize Destroys[8]{};
inline foundation::usize Draws = 0;
inline internal::RasterPacket Packet;
inline foundation::uint8 BufferBytes[internal::RASTER_CAPACITY][internal::LIFETIME_TRANSFER_BYTES]{};
inline foundation::usize BufferSizes[internal::RASTER_CAPACITY]{};
inline foundation::uint8 ReadbackBytes[internal::LIFETIME_TRANSFERS][internal::LIFETIME_TRANSFER_BYTES]{};
inline RasterStatus Transfers[2][internal::LIFETIME_TRANSFERS];
inline bool TransferOccupied[2][internal::LIFETIME_TRANSFERS]{};
inline bool RetainTransfersOnReset = false;
inline RasterStatus TransferStart = RasterStatus::Ready;
inline RasterStatus ReadbackCopy = RasterStatus::Ready;
inline foundation::usize Discards = 0;
inline foundation::usize Copies = 0;
void Reset() noexcept;
} // namespace ludus::graphics::rhi::reference
