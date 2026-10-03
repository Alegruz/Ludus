#pragma once
#include "internal/decode.hpp"
#include <ludus/audio/audio_source.h>
namespace ludus::audio
{
struct PreparedClip::Impl final
{
    internal::DecodedPcm Decoded;
    uint64 LoopBegin = 0;
    uint64 LoopEnd = 0;
    uint64 AssetHash = 0;
    uint32 Rate = 0;
    ~Impl() noexcept;
};
} // namespace ludus::audio
