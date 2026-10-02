#include <ludus/audio/audio_system.h>

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>

#include "internal/decode.hpp"
#include "internal/impl.hpp"
#include "internal/log_categories.h"

#include <cmath>
#include <new>

// This translation unit completes AudioSystem::Impl (storage defined in
// internal/impl.hpp) with the boundary/render/service logic.

namespace ludus::audio
{
namespace
{
[[nodiscard]] bool IsFiniteF(float32 v) noexcept
{
    return std::isfinite(v);
}
} // namespace

// ---------------------------------------------------------------------------
// Boundary processing
// ---------------------------------------------------------------------------
void AudioSystem::Impl::ConsumeAndApplyBatch() noexcept
{
    // Apply at most CONTROL_BOUNDARY_BUDGET records this boundary. Never split a
    // published batch; the ring already stores whole batches, and we only ever
    // publish a batch when it fits the remaining budget is checked by the
    // producer. Here we drain up to the budget.
    QueuedCommand cmd{};
    uint32 applied = 0;
    while (applied < CONTROL_BOUNDARY_BUDGET && CommandRing.Peek(cmd))
    {
        CommandRing.ConsumeOne();
        ++applied;
        ++AcceptedCommands;

        switch (cmd.Kind)
        {
            case CommandKind::Play: {
                VoiceSlot& v = Voices[cmd.VoiceSlotIndex];
                // Reject a stale command targeting a reused slot.
                if (v.Generation.Value != cmd.VoiceGeneration || !v.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                // Cancellation precedence: an admission epoch older than the live
                // StopAll epoch is cancelled even though it reached the ring.
                if (cmd.AdmissionEpoch < StopAllEpoch)
                {
                    TerminateVoice(cmd.VoiceSlotIndex, TerminalReason::Cancelled, StateCause::StopAllEpoch);
                    break;
                }
                // An individual Stop stored before consumption cancels the pending
                // play (a Stop on Pending waits for this drain).
                const uint32 stopGen = v.Stop.StopGeneration.load(std::memory_order_acquire);
                if (stopGen == v.Generation.Value)
                {
                    v.PlayDrained = true;
                    TerminateVoice(cmd.VoiceSlotIndex, TerminalReason::Cancelled, StateCause::Cancelled);
                    break;
                }
                v.PlayDrained = true;
                v.State = VoiceState::Scheduled;
                v.LastCause = StateCause::PlayConsumed;
                v.StartFrame = cmd.StartFrame == 0 ? RenderFrame : cmd.StartFrame;
                // A command applied after its desired frame starts at this
                // boundary and reports LateStart + lateness; never rewind or
                // silently skip the transient.
                if (cmd.StartFrame != 0 && cmd.StartFrame < RenderFrame)
                {
                    v.LateStart = true;
                    v.Lateness = RenderFrame - cmd.StartFrame;
                    v.StartFrame = RenderFrame;
                }
                break;
            }
            case CommandKind::UpdateVoice: {
                VoiceSlot& v = Voices[cmd.VoiceSlotIndex];
                if (v.Generation.Value != cmd.VoiceGeneration || !v.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                // A stopped generation cannot be revived by a later Update.
                if (v.State == VoiceState::Stopping || v.State == VoiceState::Terminal)
                {
                    ++StaleCommands;
                    break;
                }
                if (cmd.SetGain && IsFiniteF(cmd.Gain))
                {
                    v.Gain = cmd.Gain;
                }
                if (cmd.SetRate && IsFiniteF(cmd.Rate))
                {
                    v.Rate = cmd.Rate;
                }
                if (cmd.SetPriority)
                {
                    v.Priority = cmd.Priority;
                }
                if (cmd.SetPosition)
                {
                    v.EmitterPosition = cmd.EmitterPosition;
                }
                break;
            }
            case CommandKind::StopVoice: {
                VoiceSlot& v = Voices[cmd.VoiceSlotIndex];
                if (v.Generation.Value != cmd.VoiceGeneration || !v.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                if (v.State == VoiceState::Mixed || v.State == VoiceState::Virtualizing)
                {
                    v.State = VoiceState::Stopping;
                    v.LastCause = StateCause::StopRequested;
                    v.FadeTarget = 0.0F;
                }
                else if (v.State == VoiceState::Virtual || v.State == VoiceState::Scheduled)
                {
                    TerminateVoice(cmd.VoiceSlotIndex, TerminalReason::Stopped, StateCause::StopRequested);
                }
                break;
            }
            case CommandKind::SetListener:
                Listener = cmd.Listener;
                break;
            case CommandKind::SetBusBaseGain:
                if (cmd.BusIndex < BusCount && IsFiniteF(cmd.BusGain))
                {
                    Buses[cmd.BusIndex].BaseGain = cmd.BusGain;
                }
                break;
            case CommandKind::SetBusUserGain:
                if (cmd.BusIndex < BusCount && IsFiniteF(cmd.BusGain))
                {
                    Buses[cmd.BusIndex].UserGain = cmd.BusGain;
                }
                break;
            case CommandKind::AddModifier: {
                internal::ModifierState& m = Modifiers[cmd.ModifierSlotIndex];
                if (m.Generation.Value != cmd.ModifierGeneration || !m.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                m.Weight = cmd.ModifierWeight;
                m.TargetWeight = cmd.ModifierWeight;
                for (uint32 i = 0; i < BUS_CAPACITY; ++i)
                {
                    m.HasBus[i] = false;
                }
                for (uint32 i = 0; i < cmd.ModifierValueCount; ++i)
                {
                    const uint32 bi = cmd.ModifierValues[i].BusIndex;
                    if (bi < BUS_CAPACITY)
                    {
                        m.Db[bi] = cmd.ModifierValues[i].TargetDb;
                        m.HasBus[bi] = true;
                    }
                }
                break;
            }
            case CommandKind::UpdateModifier: {
                internal::ModifierState& m = Modifiers[cmd.ModifierSlotIndex];
                if (m.Generation.Value != cmd.ModifierGeneration || !m.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                m.TargetWeight = cmd.ModifierWeight;
                m.Weight = cmd.ModifierWeight;
                break;
            }
            case CommandKind::RemoveModifier: {
                internal::ModifierState& m = Modifiers[cmd.ModifierSlotIndex];
                if (m.Generation.Value != cmd.ModifierGeneration || !m.InUse)
                {
                    ++StaleCommands;
                    break;
                }
                // Fade weight to zero before releasing the slot; A3 adds the ramp.
                m.Removing = true;
                m.TargetWeight = 0.0F;
                m.Weight = 0.0F;
                m.InUse = false; // A1: immediate; A3 defers to acknowledged fade-out
                break;
            }
        }
    }
}

void AudioSystem::Impl::ResolveScheduledStarts() noexcept
{
    const uint64 quantumEnd = RenderFrame + CONTROL_QUANTUM_FRAMES;
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        VoiceSlot& v = Voices[i];
        if (!v.InUse || v.State != VoiceState::Scheduled)
        {
            continue;
        }
        if (v.StartFrame < quantumEnd)
        {
            // Enter Mixed (A1: no physical-slot contention modeling yet; A2 adds
            // selection competition). Charge selection in SelectAndCharge.
            v.State = VoiceState::Mixed;
            v.LastCause = StateCause::StartFrameReached;
            v.FadeGain = 0.0F;
            v.FadeTarget = 1.0F;
            v.CursorFrame = 0;
            v.CursorFraction = 0.0;
        }
    }
}

void AudioSystem::Impl::SelectAndCharge() noexcept
{
    // Reset per-boundary group selection counters.
    for (uint32 g = 0; g < GroupCount; ++g)
    {
        Groups[g].Selected = 0;
        Groups[g].Fading = 0;
    }
    // A1: deterministic admission charges are maintained at submit time
    // (MaxAdmitted). Steady-state MaxSelected selection competition is A2. Here
    // we only tally selected/fading for diagnostics.
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        VoiceSlot& v = Voices[i];
        if (!v.InUse)
        {
            continue;
        }
        if (v.IsStream)
        {
            continue;
        }
        if (v.State == VoiceState::Mixed)
        {
            if (v.GroupIndex < GroupCount)
            {
                ++Groups[v.GroupIndex].Selected;
            }
        }
        else if (v.State == VoiceState::Virtualizing || v.State == VoiceState::Stopping)
        {
            if (v.GroupIndex < GroupCount)
            {
                ++Groups[v.GroupIndex].Fading;
            }
        }
    }
}

void AudioSystem::Impl::AcknowledgeTerminals() noexcept
{
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        VoiceSlot& v = Voices[i];
        if (!v.InUse)
        {
            continue;
        }
        if (v.State == VoiceState::Terminal)
        {
            // Durable terminal acknowledgment: the renderer already published
            // TerminalGeneration in TerminateVoice. The owner (same thread in
            // A1 Offline mode) reclaims storage and releases the asset pin and
            // the group admission charge.
            if (v.GroupIndex < GroupCount && Groups[v.GroupIndex].Admitted > 0)
            {
                --Groups[v.GroupIndex].Admitted;
            }
            ReleaseVoiceSlot(i);
        }
    }
}

void AudioSystem::Impl::TerminateVoice(uint32 slot, TerminalReason reason, StateCause cause) noexcept
{
    VoiceSlot& v = Voices[slot];
    v.State = VoiceState::Terminal;
    v.LastCause = cause;
    v.Terminal.Reason = reason;
    v.Terminal.FinalFrame = RenderFrame;
    // Release-publish the terminal generation; the owner acquires it to reclaim.
    v.Terminal.TerminalGeneration.store(v.Generation.Value, std::memory_order_release);
}

void AudioSystem::Impl::ProcessControlBoundary() noexcept
{
    ConsumeAndApplyBatch();
    ResolveScheduledStarts();
    RecomputeBusGains();
    SelectAndCharge();
}

// ---------------------------------------------------------------------------
// Render (design section 5). A1 fills the output completely with silence for
// the actual samples (no PCM read yet); it still advances the sample clock,
// processes control boundaries at 128-frame multiples, and resolves EOF for
// finite voices so lifetimes are correct. A2 installs the resident DSP.
// ---------------------------------------------------------------------------
Status AudioSystem::Impl::RenderFrames(std::span<float32> output,
                                       ChannelLayout layout,
                                       BufferLayout bufferLayout,
                                       uint32 frames) noexcept
{
    const uint32 channels = ChannelCount(layout);
    if (channels != 2)
    {
        // v1 output is stereo; reject other layouts rather than guessing.
        return Status::Unsupported;
    }
    if (frames == 0)
    {
        return Status::Ok; // zero frames is a no-op
    }
    // Checked capacity: SampleValues = frames * channels.
    const uint64 required = static_cast<uint64>(frames) * channels;
    if (output.size() < required)
    {
        return Status::InvalidArgument;
    }

    // Zero the whole output up front (silence is the A1 signal). Interleaved and
    // planar both just clear here.
    for (uint64 i = 0; i < required; ++i)
    {
        output[i] = 0.0F;
    }
    (void)bufferLayout;

    // Walk the request in 128-frame control quanta, applying a boundary at each
    // multiple (including frame zero within this call when aligned).
    uint32 produced = 0;
    while (produced < frames)
    {
        if (RenderFrame % CONTROL_QUANTUM_FRAMES == 0)
        {
            ProcessControlBoundary();
        }
        const uint32 toNextBoundary =
            CONTROL_QUANTUM_FRAMES - static_cast<uint32>(RenderFrame % CONTROL_QUANTUM_FRAMES);
        uint32 span = frames - produced;
        if (span > toNextBoundary)
        {
            span = toNextBoundary;
        }

        // Advance finite voices for lifetime correctness. In A1 the output stays
        // silent, but EOF/stop lifetimes still resolve so group/pin accounting
        // is exercised. A2 replaces this with the actual DSP read.
        for (uint32 i = 0; i < LogicalCapacity; ++i)
        {
            VoiceSlot& v = Voices[i];
            if (!v.InUse)
            {
                continue;
            }
            if (v.State == VoiceState::Mixed || v.State == VoiceState::Virtual || v.State == VoiceState::Virtualizing ||
                v.State == VoiceState::Stopping)
            {
                v.CursorFrame += span; // placeholder transport advance (A2: rate-aware)
            }
        }

        RenderFrame += span;
        produced += span;
    }

    return Status::Ok;
}

void AudioSystem::Impl::FillSnapshot(SystemSnapshot& s) const noexcept
{
    s = SystemSnapshot{};
    s.State = State;
    s.SystemMode = SystemMode;
    s.Session = Session;
    s.SampleRate = SampleRate;
    s.RenderFrame = RenderFrame;
    s.QueueDepth = CommandRing.Available();
    s.QueueHighWater = CommandRing.HighWater();
    s.AcceptedCommands = AcceptedCommands;
    s.RejectedCommands = RejectedCommands;
    s.StaleCommands = StaleCommands;

    uint32 pending = 0;
    uint32 mixed = 0;
    uint32 virt = 0;
    uint32 fading = 0;
    uint32 streams = 0;
    uint32 unreclaimed = 0;
    uint32 clips = 0;
    uint64 pcmBytes = 0;
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        const VoiceSlot& v = Voices[i];
        if (!v.InUse)
        {
            continue;
        }
        switch (v.State)
        {
            case VoiceState::Pending:
            case VoiceState::Scheduled:
                ++pending;
                break;
            case VoiceState::Mixed:
                v.IsStream ? ++streams : ++mixed;
                break;
            case VoiceState::Virtual:
                ++virt;
                break;
            case VoiceState::Virtualizing:
            case VoiceState::Stopping:
                ++fading;
                break;
            case VoiceState::Terminal:
                ++unreclaimed;
                break;
        }
    }
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        const internal::ClipSlot& c = Clips[i];
        if (c.InUse)
        {
            ++clips;
            pcmBytes += c.SampleValues * sizeof(float32);
        }
    }
    s.PendingVoices = pending;
    s.MixedVoices = mixed;
    s.VirtualVoices = virt;
    s.FadingVoices = fading;
    s.StreamVoices = streams;
    s.LogicalHighWater = LogicalHighWater;
    s.UnreclaimedTerminals = unreclaimed;
    s.StreamStarvations = StreamStarvations;
    s.EofEvents = EofEvents;
    s.Errors = Errors;
    s.ResidentClips = clips;
    s.ResidentPcmBytes = pcmBytes;
    s.SnapshotLosses = SnapshotLosses;
    s.PreClipFrames = PreClipFrames;
    s.NonFiniteFaults = NonFiniteFaults;
}

void AudioSystem::Impl::PublishSnapshot() noexcept
{
    internal::SnapshotRecord rec{};
    FillSnapshot(rec.System);
    std::span<const internal::SnapshotRecord> one(&rec, 1);
    if (!SnapshotRing.TryPublishBatch(one))
    {
        ++SnapshotLosses;
    }
}

} // namespace ludus::audio
