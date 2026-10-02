#pragma once

// Opt-in owner-side diagnostics for the Ludus audio system, per
// .kiro/specs/audio/design.md sections 9 and 11 and requirement AU16.
//
// The sink is drained on the owner/control thread during Service(); it is never
// called from the device/worklet callback or the decode worker. It receives
// bounded numeric records (no strings built on the audio thread, no allocation
// in rendering). Correlation of owner-side preparation/admission/policy
// failures uses the caller's numeric tag and status; no phantom voice is created.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include <span>

namespace ludus::audio
{
// An owner-side policy/admission diagnostic that is NOT a voice: a suppressed
// trigger, a rejected batch, or a failed preparation, tagged with the caller's
// numeric key so it can be correlated without inventing a VoiceHandle.
enum class OwnerEventKind : uint8
{
    BatchRejected,
    PreparationFailed,
    PolicySuppressed, // application-adapter cooldown / AlreadyActive
    QueueFull,
    GroupRejected,
    VoiceCapacityRejected,
};

enum class PolicyCause : uint8
{
    None,
    Cooldown,
    AlreadyActive,
    TableFull,
};

struct OwnerEvent final
{
    OwnerEventKind Kind = OwnerEventKind::BatchRejected;
    Status Result = Status::InvalidArgument;
    PolicyCause Policy = PolicyCause::None;
    uint32 PolicyTag = 0;        // caller numeric tag
    uint32 OwnerKey = 0;         // application owner tag
    uint64 ApplicationClock = 0; // owner-chosen clock (e.g. sim tick)
};

// A single trace record at its actual applied sample frame (design section 11).
struct TraceRecord final
{
    uint64 RenderFrame = 0;
    CommandKind Kind = CommandKind::Play;
    VoiceHandle Voice{};
    uint32 Variant = 0; // selected variation index (never re-rolled on reentry)
    uint32 Seed = 0;
    StateCause Cause = StateCause::None;
};

// The owner drains these during Service. Implementations copy what they need and
// return quickly; they must not call back into the AudioSystem. Loss of debug
// records is counted and never blocks terminal acknowledgment or reclamation.
class AudioDebugSnapshotSink
{
public:
    virtual ~AudioDebugSnapshotSink() = default;

    // A full system snapshot copied on the owner thread.
    virtual void OnSystemSnapshot(const SystemSnapshot& snapshot) noexcept = 0;

    // Per-voice snapshots for the current publication (borrowed; valid only for
    // the duration of the call).
    virtual void OnVoiceSnapshots(std::span<const VoiceInfo> voices) noexcept = 0;

    // Owner-side policy/admission/preparation diagnostics (no phantom voice).
    virtual void OnOwnerEvent(const OwnerEvent& event) noexcept = 0;

    // Optional bounded trace records (may be truncated; truncation is counted
    // separately from gameplay admission loss).
    virtual void OnTrace(std::span<const TraceRecord> records) noexcept = 0;
};

} // namespace ludus::audio
