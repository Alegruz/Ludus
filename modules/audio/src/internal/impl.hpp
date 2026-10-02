#pragma once

// AudioSystem::Impl — the control owner's complete private storage and the
// declarations of its member logic, per .kiro/specs/audio/design.md sections
// 3-11. The struct lives in this internal header so the implementation can be
// split across translation units (validation/slots/bus in audio_system.cpp;
// boundary/render/façade in control_owner.cpp) while both see identical storage.
//
// All fixed storage is embedded and allocated exactly once when Impl is
// new'd at Initialize. Warm paths (submit/render/query) allocate/free nothing,
// take no lock, perform no I/O and invoke no gameplay callback.

#include <ludus/audio/audio_debug.h>
#include <ludus/audio/audio_system.h>
#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/types.h>

#include "internal/clip_store.hpp"
#include "internal/control_owner.hpp"
#include "internal/handles.hpp"
#include "internal/mixer.hpp"
#include "internal/spsc_ring.hpp"

#include <span>

namespace ludus::audio
{
using internal::GroupState;
using internal::QueuedCommand;
using internal::SpscRing;
using internal::VoiceSlot;

struct AudioSystem::Impl final
{
    // --- Configuration (frozen at Initialize) ------------------------------
    SystemConfig Config{};
    uint32 Session = 0;
    SystemState State = SystemState::Disabled;
    Mode SystemMode = Mode::Disabled;
    uint32 SampleRate = 0;
    ShutdownState Shutdown = ShutdownState::NotStarted;

    uint32 LogicalCapacity = static_cast<uint32>(LOGICAL_VOICE_CAPACITY);
    uint32 MixedCapacity = static_cast<uint32>(MIXED_VOICE_CAPACITY);
    uint32 StreamInstances = static_cast<uint32>(STREAM_CAPACITY);
    uint32 BusCount = 1;
    uint32 GroupCount = 1;

    // --- Sample clock (design section 5) -----------------------------------
    uint64 RenderFrame = 0;

    // --- Cancellation epoch (StopAll) --------------------------------------
    uint32 StopAllEpoch = 1;

    // --- Fixed storage -----------------------------------------------------
    VoiceSlot Voices[LOGICAL_VOICE_CAPACITY]{};
    internal::ClipSlot Clips[LOGICAL_VOICE_CAPACITY]{};
    GroupState Groups[GROUP_CAPACITY]{};
    internal::BusState Buses[BUS_CAPACITY]{};
    internal::ModifierState Modifiers[MODIFIER_CAPACITY]{};
    ListenerPose Listener{};

    SpscRing<QueuedCommand, COMMAND_QUEUE_CAPACITY> CommandRing;
    SpscRing<internal::SnapshotRecord, 4> SnapshotRing;

    VoiceHandle LastBatchVoices[MAX_BATCH_RECORDS]{};
    uint32 LastBatchVoiceCount = 0;

    // Diagnostics.
    uint64 AcceptedCommands = 0;
    uint64 RejectedCommands = 0;
    uint64 StaleCommands = 0;
    uint32 QueueHighWater = 0;
    uint32 LogicalHighWater = 0;
    uint64 StreamStarvations = 0;
    uint64 EofEvents = 0;
    uint64 Errors = 0;
    uint32 SnapshotLosses = 0;
    uint64 PreClipFrames = 0;
    uint64 NonFiniteFaults = 0;

    AudioDebugSnapshotSink* Sink = nullptr;

    // Scratch for one batch's resolved commands (bounded).
    QueuedCommand BatchScratch[MAX_BATCH_RECORDS]{};

    // --- Validation / admission (audio_system.cpp) -------------------------
    [[nodiscard]] bool ValidatePlay(const PlayParams& p) const noexcept;
    [[nodiscard]] Status ValidateConfig(const SystemConfig& cfg) const noexcept;

    [[nodiscard]] uint32 ReserveVoiceSlot() noexcept;
    void ReleaseVoiceSlot(uint32 index) noexcept;

    [[nodiscard]] uint32 FindClipSlot(ClipHandle h) const noexcept;
    [[nodiscard]] uint32 FindVoiceSlot(VoiceHandle h) const noexcept;
    [[nodiscard]] uint32 FindModifierSlot(ModifierHandle h) const noexcept;

    [[nodiscard]] VoiceHandle MakeVoiceHandle(uint32 slot) const noexcept;
    [[nodiscard]] ClipHandle MakeClipHandle(uint32 slot) const noexcept;

    // --- Boundary / render (control_owner.cpp) -----------------------------
    void ProcessControlBoundary() noexcept;
    void ConsumeAndApplyBatch() noexcept;
    void ResolveScheduledStarts() noexcept;
    void SelectAndCharge() noexcept;
    void AcknowledgeTerminals() noexcept;

    void RecomputeBusGains() noexcept;
    [[nodiscard]] float32 BusChainGain(uint32 busIndex) const noexcept;

    [[nodiscard]] Status
    RenderFrames(std::span<float32> output, ChannelLayout layout, BufferLayout bufferLayout, uint32 frames) noexcept;

    void PublishSnapshot() noexcept;
    void TerminateVoice(uint32 slot, TerminalReason reason, StateCause cause) noexcept;
};

} // namespace ludus::audio
