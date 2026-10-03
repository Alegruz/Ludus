#pragma once

// Resident clip decode seam, per .kiro/specs/audio/design.md sections 2 and 9.
// A1 declares the contract and provides a WAV/synthetic path sufficient for the
// offline gym and tests; A2 wires the pinned private miniaudio 0.11.23 WAV/FLAC
// decoder behind the same signature. Decoding is a cold operation off rendering;
// it owns the produced finite float32 PCM at the session rate and reports
// failure explicitly (never throws). Decoded bytes are counted against the
// resident PCM cap by the caller.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include <span>

namespace ludus::audio::internal
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;

// Result of a cold decode into owned PCM. On success Pcm is a heap buffer the
// caller takes ownership of (freed with ::operator delete[]).
struct DecodedPcm final
{
    Status Result = Status::DecodeError;
    float32* Pcm = nullptr; // interleaved, Frames * Channels samples
    uint64 Frames = 0;
    uint64 SourceFrames = 0;
    uint32 SourceRate = 0;
    uint32 Channels = 1;
    ChannelLayout Layout = ChannelLayout::Mono;
    float32 PreparedPeak = 0.0F;
};

// Decode `encoded` of `format` into float32 PCM resampled to `sessionRate`.
// Validates source bytes, frame/channel/size/finite. On failure returns a
// specific Status and leaves Pcm null. Implemented in decode.cpp.
[[nodiscard]] DecodedPcm
DecodeResidentClip(std::span<const ludus::foundation::uint8> encoded, SourceFormat format, uint32 sessionRate) noexcept;

} // namespace ludus::audio::internal
