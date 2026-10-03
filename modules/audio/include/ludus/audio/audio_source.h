#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/audio/audio_types.h>

#include <span>

namespace ludus::audio
{
// Cold decoder input owned by the stream instance. Calls occur only during
// explicit preparation or on its decode worker, never on the renderer.
class StreamInput
{
public:
    virtual ~StreamInput() noexcept = default;
    [[nodiscard]] virtual Status Read(std::span<uint8> bytes, usize& count) noexcept = 0;
    [[nodiscard]] virtual Status Seek(foundation::int64 offset, bool relative) noexcept = 0;
};
class AudioSystem;
// A worker-owned decoded candidate. Decode has no AudioSystem access; install
// transfers immutable storage on the sole control owner after capacity checks.
class PreparedClip final
{
public:
    PreparedClip() noexcept = default;
    ~PreparedClip() noexcept;
    PreparedClip(const PreparedClip&) = delete;
    PreparedClip& operator=(const PreparedClip&) = delete;
    [[nodiscard]] Status Decode(std::span<const uint8> encoded, const ClipDescriptor& descriptor, uint32 rate) noexcept;
    [[nodiscard]] uint64 Bytes() const noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
    friend class AudioSystem;
};
struct SourceInfo final
{
    uint64 Frames = 0;
    uint32 SampleRate = 0;
    uint32 Channels = 0;
    float32 Peak = 0.0F;
};
// Cold, bounded scratch decode. peaks receives max absolute values in equal
// source-frame ranges; an empty span requests metadata without a full scan.
[[nodiscard]] Status InspectSource(std::span<const uint8> encoded,
                                   SourceFormat format,
                                   SourceInfo& output,
                                   std::span<float32> peaks = {}) noexcept;
[[nodiscard]] Status
InspectSource(StreamInput& input, SourceFormat format, SourceInfo& output, std::span<float32> peaks = {}) noexcept;
} // namespace ludus::audio
