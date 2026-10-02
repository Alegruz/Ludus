#pragma once

// A thin application-owned presentation/event adapter between typed game
// actions/data and audio commands, per .kiro/specs/audio/design.md section 9
// ("Application presentation and event policy") and requirement AU16. This is
// an application EXAMPLE built on the public AudioSystem API, not an engine
// policy: it copies resolved numeric parameters, tracks accepted handles, owns
// loop/modifier lifetimes, and enforces a bounded cooldown / AlreadyActive
// suppression BEFORE admission. It holds no entity pointers and runs nothing on
// the renderer. No ECS, event bus or MVVM framework.
//
// Clock: the adapter uses an application-chosen clock (e.g. simulation ticks)
// passed into each call, never a stale RenderFrame snapshot.

#include <ludus/audio/audio_debug.h>
#include <ludus/audio/audio_system.h>
#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

namespace ludus::audio::app
{
using ludus::foundation::float32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

inline constexpr usize POLICY_TABLE_CAPACITY = 64; // fixed policy table size
inline constexpr usize ADAPTER_LOOP_CAPACITY = 64; // tracked owned loops
inline constexpr usize DIALOGUE_MEMBER_CAPACITY = 16;

// Why an event was suppressed before admission (owner-side; no VoiceHandle).
enum class SuppressReason : uint8
{
    None,
    Cooldown,
    AlreadyActive,
    TableFull,
};

// Result of a policed trigger. On Admitted the Voice is valid; otherwise the
// Reason explains the owner-side suppression (no activity/duck created).
struct TriggerResult final
{
    bool Admitted = false;
    VoiceHandle Voice{};
    SuppressReason Suppressed = SuppressReason::None;
    Status EngineStatus = Status::Ok; // engine admission status when attempted
};

// A policed one-shot/loop request. EventId + OwnerTag key the policy table.
struct TriggerRequest final
{
    uint32 EventId = 0;              // numeric event id (policy key part 1)
    uint32 OwnerTag = 0;             // generation-aware owner tag (policy key part 2)
    uint64 CooldownTicks = 0;        // minimum application ticks between admissions
    bool SuppressWhileActive = true; // AlreadyActive suppression
    PlayParams Play{};               // resolved numeric parameters (copied)
    bool IsOwnedLoop = false;        // track for explicit teardown on owner removal
};

// A thin adapter owning a bounded policy table, tracked owned loops, and one
// shared dialogue duck context. All members are fixed-size; it allocates nothing
// after construction.
class EventAdapter final
{
public:
    explicit EventAdapter(AudioSystem& system) noexcept;

    EventAdapter(const EventAdapter&) = delete;
    EventAdapter& operator=(const EventAdapter&) = delete;

    // Evaluate policy at application clock `nowTicks`, then (only if not
    // suppressed) attempt admission. Policy state (cooldown/active) updates ONLY
    // on successful engine admission; a QueueFull/NotReady/GroupCapacity failure
    // consumes no cooldown and leaves no active/duck state. A full policy table
    // for a new key returns TableFull. Reports owner events to the attached sink.
    [[nodiscard]] TriggerResult Trigger(const TriggerRequest& req, uint64 nowTicks) noexcept;

    // Call every service tick: observe durable terminals so active/cooldown and
    // dialogue-duck membership release correctly, and reclaim expired entries.
    void Update(uint64 nowTicks) noexcept;

    // Stop all owned loops for `ownerTag` (entity removal). Detached one-shots
    // are not tracked here and may finish naturally.
    void StopOwnedLoops(uint32 ownerTag) noexcept;

    // --- Overlapping dialogue duck (design section 8/9) -----------------
    // Begin/trigger a dialogue line that participates in a single shared duck
    // modifier on `duckBusIndex`. The duck is created on the first accepted
    // member and released only after the last member durably terminates. A
    // failed admission creates no member and no duck.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named duck config.
    void ConfigureDialogueDuck(uint32 duckBusIndex, float32 duckDb) noexcept;
    [[nodiscard]] TriggerResult TriggerDialogue(const TriggerRequest& req, uint64 nowTicks) noexcept;
    [[nodiscard]] bool DialogueDuckActive() const noexcept;
    [[nodiscard]] uint32 DialogueMemberCount() const noexcept;

    void SetSink(AudioDebugSnapshotSink* sink) noexcept
    {
        mSink = sink;
    }

private:
    struct PolicyEntry final
    {
        bool InUse = false;
        uint32 EventId = 0;
        uint32 OwnerTag = 0;
        uint64 LastAdmitTick = 0;
        uint64 CooldownTicks = 0;
        bool HasActive = false;
        VoiceHandle Active{};
    };

    struct OwnedLoop final
    {
        bool InUse = false;
        uint32 OwnerTag = 0;
        VoiceHandle Voice{};
    };

    struct DialogueMember final
    {
        bool InUse = false;
        VoiceHandle Voice{};
    };

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): policy table key.
    [[nodiscard]] uint32 FindPolicy(uint32 eventId, uint32 ownerTag) const noexcept;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): policy table key.
    [[nodiscard]] uint32 AllocPolicy(uint32 eventId, uint32 ownerTag) noexcept;
    [[nodiscard]] bool VoiceStillActive(VoiceHandle v) const noexcept;
    void TrackLoop(uint32 ownerTag, VoiceHandle v) noexcept;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named diagnostic fields.
    void
    EmitOwner(OwnerEventKind kind, Status status, PolicyCause cause, uint32 tag, uint32 owner, uint64 clock) noexcept;

    AudioSystem& mSystem;
    AudioDebugSnapshotSink* mSink = nullptr;
    PolicyEntry mPolicy[POLICY_TABLE_CAPACITY]{};
    OwnedLoop mLoops[ADAPTER_LOOP_CAPACITY]{};

    // Shared dialogue duck.
    uint32 mDuckBus = 0;
    float32 mDuckDb = -6.0F;
    bool mDuckConfigured = false;
    bool mDuckActive = false;
    ModifierHandle mDuck{};
    DialogueMember mDialogue[DIALOGUE_MEMBER_CAPACITY]{};
};

} // namespace ludus::audio::app
