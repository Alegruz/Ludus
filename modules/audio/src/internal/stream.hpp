#pragma once
#include <ludus/audio/audio_source.h>
#include <ludus/audio/audio_types.h>

#include <atomic>
#include <span>

namespace ludus::audio::internal
{
struct StreamChunk final
{
    float32 Samples[STREAM_CHUNK_FRAMES * 2]{};
    uint32 Frames = 0;
    bool Eof = false;
    bool Error = false;
};
struct StreamRequest final
{
    uint32 Frames = 0;
    float32 Gain = 1.0F;
    bool Stop = false;
};
struct StreamResult final
{
    uint32 Frames = 0;
    bool Terminal = false;
    bool Error = false;
};
struct StreamData final
{
    StreamChunk Chunks[STREAM_CHUNK_COUNT];
    std::atomic<uint32> Written{0};
    std::atomic<uint32> Read{0};
    std::atomic<bool> Cancel{false};
    std::atomic<bool> Done{false};
    void* Decoder = nullptr;
    uint8* Encoded = nullptr;
    uint64 EncodedBytes = 0;
    StreamInput* Input = nullptr;
    uint32 Rate = 0;
    uint64 Begin = 0;
    uint64 End = 0;
    uint64 DecodeCursor = 0;
    bool Looping = false;
    bool WorkerEof = false;
    // Renderer-exclusive fields.
    uint32 Offset = 0;
    float32 Fade = 0.0F;
    float32 LastL = 0.0F;
    float32 LastR = 0.0F;
    ~StreamData() noexcept;
    [[nodiscard]] Status Open(std::span<const uint8> bytes, const StreamDescriptor& descriptor, uint32 rate) noexcept;
    [[nodiscard]] Status Open(StreamInput* input, const StreamDescriptor& descriptor, uint32 rate) noexcept;
    [[nodiscard]] Status Configure(const StreamDescriptor& descriptor, uint32 rate) noexcept;
    void Refill() noexcept;
    [[nodiscard]] StreamResult Render(float32* output, const StreamRequest& request) noexcept;
};
struct StreamSlot final
{
    uint32 Generation = 0;
    bool InUse = false;
    bool Played = false;
    bool Retiring = false;
    StreamData* Data = nullptr;
};
} // namespace ludus::audio::internal
