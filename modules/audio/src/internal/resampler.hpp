#pragma once

// Audited, preinitialized rate converter wrapping the pinned private miniaudio
// 0.11.23 linear resampler, per .kiro/specs/audio/design.md sections 2, 5 and 6.
//
// Contract:
//  - All heap is acquired once at Init (outside rendering) via miniaudio's
//    external-heap API; Process allocates/frees nothing, takes no lock and does
//    no I/O. Reset/SetRate are bounded.
//  - The conversion ratio is derived from source/session sample rates and the
//    playback Rate, NEVER from supplied buffer lengths. Fractional phase and
//    filter history carry across partial chunks. Produced/consumed counts are
//    reported exactly; input frames need not equal output frames.
//  - Downsampling uses low-pass filtering (lpfOrder) for anti-aliasing.
//
// A single resampler instance is preinitialized per resident physical voice
// slot; the mixer never inits/uninits it in Play/Stop. The source channel count
// is fixed at Init; a mono/stereo voice uses its own channel configuration.

#include <ludus/foundation/base/types.h>

#include <cstddef>

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;

// Opaque storage for one miniaudio resampler plus its external heap. Sized at
// runtime from the dependency's reported heap size. Owned here (freed at
// Uninit). No miniaudio type leaks into any public header.
class Resampler final
{
public:
    Resampler() noexcept = default;
    ~Resampler() noexcept;

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;
    Resampler(Resampler&&) = delete;
    Resampler& operator=(Resampler&&) = delete;

    // Preinitialize for `channels` (1 or 2) converting rateIn->rateOut. Allocates
    // its heap once here. Returns false on failure (reported by the caller as
    // OutOfMemory / InvalidArgument). lpfOrder 0 disables filtering.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named rate args.
    [[nodiscard]] bool Init(uint32 channels, uint32 rateIn, uint32 rateOut, uint32 lpfOrder) noexcept;
    void Uninit() noexcept;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return mBackend != nullptr;
    }

    // Change the output/input rate ratio without reallocating. Bounded; keeps
    // phase/history. rateIn/rateOut are the effective rates (session rate and
    // the rate implied by playback Rate). Returns false if the backend lacks
    // rate-change support (then the caller keeps a fixed ratio).
    [[nodiscard]] bool SetRate(uint32 rateIn, uint32 rateOut) noexcept;

    // Reset phase/history (used only outside rendering, e.g. before reuse).
    void Reset() noexcept;

    // Convert interleaved float32 input to interleaved float32 output. On entry
    // *frameCountIn / *frameCountOut are capacities; on return they hold the
    // frames actually consumed / produced. Bounded, no allocation.
    [[nodiscard]] bool Process(const float32* in, uint64* frameCountIn, float32* out, uint64* frameCountOut) noexcept;

    // How many input frames are required to produce `outputFrames` output frames
    // at the current ratio (for bounded scratch sizing). Returns 0 on failure.
    [[nodiscard]] uint64 RequiredInputFrames(uint64 outputFrames) const noexcept;

    [[nodiscard]] uint32 Channels() const noexcept
    {
        return mChannels;
    }

private:
    void* mBackend = nullptr; // ma_resampler*
    void* mHeap = nullptr;    // external heap for the backend
    uint32 mChannels = 0;
};

} // namespace ludus::audio::internal
