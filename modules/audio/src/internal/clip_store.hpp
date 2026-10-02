#pragma once

// Immutable prepared resident PCM storage, per .kiro/specs/audio/design.md
// sections 3, 6 and 7. A clip owns finite float32 PCM at the session rate and is
// shared immutably across voices; each voice keeps its own cursor. Clips are
// pinned while any pending/active/fading voice references them and reclaimed only
// after terminal + worker acknowledgment. Restart invalidates old handles.
//
// A1 provides the ownership/pin/retire bookkeeping and a seam to install PCM.
// A2 fills the decode path (WAV/FLAC) behind PrepareClip; tests and the offline
// gym can install synthetic PCM directly through the internal seam.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include "internal/handles.hpp"

#include <atomic>
#include <new>

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::usize;

// One resident clip slot. PCM is heap-owned (allocated outside rendering) and
// released only after the pin count reaches zero following retirement.
struct ClipSlot final
{
    SlotGeneration Generation{};
    bool InUse = false;
    bool Retiring = false; // admission closed; awaiting pins to drain

    ChannelLayout Layout = ChannelLayout::Mono;
    uint32 Channels = 1;
    uint64 Frames = 0;    // frames of prepared PCM at session rate
    uint64 LoopBegin = 0; // prepared-rate, half-open [LoopBegin, LoopEnd)
    uint64 LoopEnd = 0;   // 0 means no loop metadata
    uint64 AssetHash = 0;
    float32 PreparedPeak = 0.0F; // whole-clip peak (conservative audibility estimate)

    // Interleaved float32 PCM, SampleValues = Frames * Channels. Owned.
    float32* Pcm = nullptr;
    uint64 SampleValues = 0;

    // Live reference pins (reservations/voices). Reclaim only at zero.
    uint32 Pins = 0;

    void ReleasePcm() noexcept
    {
        ::operator delete[](Pcm, std::nothrow);
        Pcm = nullptr;
        SampleValues = 0;
    }
};

} // namespace ludus::audio::internal
