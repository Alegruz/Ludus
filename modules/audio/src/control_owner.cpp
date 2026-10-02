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

    // Build a deterministic ordered candidate list of resident voices eligible
    // for steady-state selection (Mixed / Virtual / Virtualizing wanting to
    // reverse). Rank by priority (desc), then estimated audibility (desc), with
    // stable ties preferring incumbents then admission (slot) order. Selection
    // uses a bounded insertion into a fixed array (O(V*P)), no heap growth.
    uint32 order[LOGICAL_VOICE_CAPACITY];
    uint32 candidateCount = 0;
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        VoiceSlot& v = Voices[i];
        if (!v.InUse || v.IsStream)
        {
            continue;
        }
        if (v.State == VoiceState::Stopping || v.State == VoiceState::Terminal || v.State == VoiceState::Pending ||
            v.State == VoiceState::Scheduled)
        {
            continue;
        }
        // Compute gains/score for this boundary (fills Score*/Pan).
        float32 gl = 0.0F;
        float32 gr = 0.0F;
        ComputeVoiceGains(v, gl, gr);
        v.TargetGainL = gl;
        v.TargetGainR = gr;

        // Insertion into the ordered list.
        const float32 estI = EstimateAudibility(v);
        uint32 pos = candidateCount;
        while (pos > 0)
        {
            const VoiceSlot& prev = Voices[order[pos - 1]];
            const float32 estP = EstimateAudibility(prev);
            bool iBefore = false;
            if (v.Priority != prev.Priority)
            {
                iBefore = v.Priority > prev.Priority;
            }
            else if (estI != estP)
            {
                iBefore = estI > estP;
            }
            else
            {
                // Stable: incumbents (currently Mixed) before non-incumbents;
                // then lower slot (earlier admission) first.
                const bool iInc = v.State == VoiceState::Mixed;
                const bool pInc = prev.State == VoiceState::Mixed;
                if (iInc != pInc)
                {
                    iBefore = iInc;
                }
                else
                {
                    iBefore = i < order[pos - 1];
                }
            }
            if (!iBefore)
            {
                break;
            }
            order[pos] = order[pos - 1];
            --pos;
        }
        order[pos] = i;
        ++candidateCount;
    }

    // Select top candidates subject to each group's MaxSelected and the global
    // resident steady-state budget. Hysteresis: within equal priority an
    // incumbent needs the challenger to exceed it (already ordered); a voice
    // below the virtualize threshold is not selected.
    uint32 globalSelected = 0;
    const uint32 globalBudget = static_cast<uint32>(RESIDENT_STEADY_SELECT);

    for (uint32 k = 0; k < candidateCount; ++k)
    {
        VoiceSlot& v = Voices[order[k]];
        const uint32 g = v.GroupIndex;
        const float32 est = EstimateAudibility(v);

        // Audibility gate: below the virtualize threshold (-60 dB ~ 0.001) a
        // voice is not selected; it virtualizes or (reentry) stays virtual until
        // above the reentry threshold (-57 dB). Mixed incumbents use the lower
        // (virtualize) threshold; virtual challengers use the higher (reentry).
        const float32 virtualizeThresh = 0.001F; // ~ -60 dB
        const float32 reentryThresh = 0.001413F; // ~ -57 dB
        const bool incumbent = v.State == VoiceState::Mixed || v.State == VoiceState::Virtualizing;
        const float32 thresh = incumbent ? virtualizeThresh : reentryThresh;

        bool select = est >= thresh;
        if (select && Groups[g].Selected >= Groups[g].MaxSelected)
        {
            select = false; // group quota reached
        }
        if (select && globalSelected >= globalBudget)
        {
            select = false; // global steady-state budget reached
        }

        if (select)
        {
            ++Groups[g].Selected;
            ++globalSelected;
            v.Silence = SilenceCause::None;
            // Transition toward Mixed. A Virtual or Virtualizing voice reverses
            // into Mixed (the Virtualizing fade reverses in place from its
            // current value); an incumbent Mixed voice simply retargets to full.
            if (v.State == VoiceState::Virtual || v.State == VoiceState::Virtualizing)
            {
                v.State = VoiceState::Mixed;
                v.LastCause = StateCause::ReselectedReversed;
            }
            v.FadeTarget = 1.0F;
        }
        else
        {
            // Not selected: follow policy. Mixed -> Virtualizing (advance) or
            // Stopping (kill). Virtualizing continues. Virtual stays virtual.
            if (est < thresh)
            {
                v.Silence |= SilenceCause::BelowThreshold;
            }
            if (Groups[g].Selected >= Groups[g].MaxSelected)
            {
                v.Silence |= SilenceCause::GroupQuota;
            }
            if (globalSelected >= globalBudget)
            {
                v.Silence |= SilenceCause::GlobalBudget;
            }
            if (v.State == VoiceState::Mixed)
            {
                if (v.Policy == VirtualPolicy::AdvanceWhenVirtual)
                {
                    v.State = VoiceState::Virtualizing;
                    v.LastCause = StateCause::LostSelectionGroupQuota;
                    v.FadeTarget = 0.0F;
                }
                else
                {
                    v.State = VoiceState::Stopping;
                    v.LastCause = StateCause::LostSelectionAudibility;
                    v.FadeTarget = 0.0F;
                }
            }
            // Virtualizing continues toward Virtual; Virtual stays.
        }
    }

    // Tally fading tails for diagnostics.
    for (uint32 i = 0; i < LogicalCapacity; ++i)
    {
        const VoiceSlot& v = Voices[i];
        if (!v.InUse || v.IsStream)
        {
            continue;
        }
        if ((v.State == VoiceState::Virtualizing || v.State == VoiceState::Stopping) && v.GroupIndex < GroupCount)
        {
            ++Groups[v.GroupIndex].Fading;
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
// Render (design sections 5-8). Walks the request in 128-frame control quanta,
// applies a boundary at each multiple, selects/binds physical voices, runs the
// resident DSP kernel into a stereo accumulator, applies the bus clamp and
// metering, and writes interleaved/planar output. Bounded, allocation-free.
// ---------------------------------------------------------------------------
Status AudioSystem::Impl::RenderFrames(std::span<float32> output,
                                       ChannelLayout layout,
                                       BufferLayout bufferLayout,
                                       uint32 frames) noexcept
{
    const uint32 channels = ChannelCount(layout);
    if (channels != 2)
    {
        return Status::Unsupported; // v1 output is stereo
    }
    if (frames == 0)
    {
        return Status::Ok;
    }
    const uint64 required = static_cast<uint64>(frames) * channels;
    if (output.size() < required)
    {
        return Status::InvalidArgument;
    }

    // Preinitialize physical voice DSP (resamplers) once, outside the per-span
    // loop (still cold relative to steady rendering; first call only).
    if (!EnsurePhysicalInitialized())
    {
        for (uint64 i = 0; i < required; ++i)
        {
            output[i] = 0.0F;
        }
        ++Errors;
        return Status::OutOfMemory;
    }

    // Clear output.
    for (uint64 i = 0; i < required; ++i)
    {
        output[i] = 0.0F;
    }

    const uint32 rampFrames = (DEFAULT_RAMP_MS * SampleRate) / 1000U;

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
        if (span > internal::VOICE_SCRATCH_FRAMES)
        {
            span = internal::VOICE_SCRATCH_FRAMES;
        }

        // Clear the stereo accumulator for this span.
        for (uint32 i = 0; i < span * 2; ++i)
        {
            MixAccum[i] = 0.0F;
        }

        // Render every mixing/fading resident voice through the DSP kernel.
        for (uint32 i = 0; i < LogicalCapacity; ++i)
        {
            VoiceSlot& v = Voices[i];
            if (!v.InUse)
            {
                continue;
            }
            const bool audiblePhase =
                v.State == VoiceState::Mixed || v.State == VoiceState::Virtualizing || v.State == VoiceState::Stopping;
            if (audiblePhase)
            {
                // Bind a physical slot if needed.
                uint32 phys = v.PhysicalIndex;
                if (phys >= MixedCapacity || !Physical[phys].Bound || Physical[phys].LogicalSlot != i)
                {
                    phys = FindPhysicalFor(i);
                    if (phys >= MixedCapacity)
                    {
                        phys = BindPhysical(i);
                    }
                    v.PhysicalIndex = phys;
                }
                if (phys >= MixedCapacity)
                {
                    // No physical slot free: wait virtually for this span.
                    v.Silence |= SilenceCause::WaitingForFadeSlot;
                    continue;
                }

                internal::PhysicalVoice& ph = Physical[phys];
                const internal::ClipSlot& c = Clips[v.ClipSlotIndex];

                // Fade target: 1 for Mixed, 0 for Virtualizing/Stopping. The
                // kernel ramps the per-channel gain from the carried current.
                const float32 fade = v.FadeTarget;
                internal::KernelInput in{};
                in.Clip = &c;
                in.Dsp = &ph.DspFor(c.Channels);
                in.SessionRate = SampleRate;
                in.Rate = v.Rate;
                in.Looping = v.Looping;
                in.LoopBegin = c.LoopBegin;
                in.LoopEnd = c.LoopEnd;
                in.TargetGainL = v.TargetGainL * fade;
                in.TargetGainR = v.TargetGainR * fade;
                in.CurrentGainL = &ph.CurrentGainL;
                in.CurrentGainR = &ph.CurrentGainR;
                in.RampFrames = rampFrames;

                const uint32 before = produced; // output frame offset
                (void)before;
                const uint32 producedFrames =
                    internal::RenderResidentVoice(in, MixAccum, span, Scratch, internal::VOICE_SCRATCH_FRAMES);

                v.CursorFrame = in.Dsp->SourceCursor;
                v.FadeGain = (in.CurrentGainL != nullptr) ? 1.0F : v.FadeGain; // informational

                // Natural EOF of a finite nonloop voice.
                if (in.Dsp->AtEof && !v.Looping)
                {
                    ++EofEvents;
                    UnbindPhysical(phys);
                    v.PhysicalIndex = 0xFFFFFFFFU;
                    TerminateVoice(i, TerminalReason::Completed, StateCause::NaturalEof);
                    continue;
                }
                // Fade completion for Virtualizing/Stopping (gain reached 0).
                if ((v.State == VoiceState::Virtualizing || v.State == VoiceState::Stopping) &&
                    ph.CurrentGainL <= 0.0001F && ph.CurrentGainR <= 0.0001F)
                {
                    UnbindPhysical(phys);
                    v.PhysicalIndex = 0xFFFFFFFFU;
                    if (v.State == VoiceState::Stopping)
                    {
                        TerminateVoice(i, TerminalReason::Stopped, StateCause::FadeComplete);
                    }
                    else
                    {
                        v.State = VoiceState::Virtual;
                        v.LastCause = StateCause::FadeComplete;
                        v.VirtualSinceFrame = RenderFrame;
                    }
                }
                (void)producedFrames;
            }
            else if (v.State == VoiceState::Virtual)
            {
                // Advance the virtual cursor without DSP. Finite nonloop voices
                // expire; this happens even while muted (advance policy).
                const internal::ClipSlot& c = Clips[v.ClipSlotIndex];
                internal::VoiceDsp tmp{};
                tmp.SourceCursor = v.CursorFrame;
                const bool alive =
                    internal::AdvanceVirtualVoice(tmp, c.Frames, v.Rate, v.Looping, c.LoopBegin, c.LoopEnd, span);
                v.CursorFrame = tmp.SourceCursor;
                if (!alive)
                {
                    ++EofEvents;
                    TerminateVoice(i, TerminalReason::Expired, StateCause::Expired);
                }
            }
        }

        // Bus headroom: sum is already in MixAccum (one bus level in A2; the
        // full bus-tree accumulation lands in A3). Count pre-clamp overs, map
        // nonfinite to zero, then clamp to [-1,1]. Write to the output span.
        for (uint32 f = 0; f < span; ++f)
        {
            float32 l = MixAccum[f * 2 + 0];
            float32 r = MixAccum[f * 2 + 1];
            if (!std::isfinite(l))
            {
                l = 0.0F;
                ++NonFiniteFaults;
            }
            if (!std::isfinite(r))
            {
                r = 0.0F;
                ++NonFiniteFaults;
            }
            if (l > 1.0F || l < -1.0F || r > 1.0F || r < -1.0F)
            {
                ++PreClipFrames;
            }
            if (l > 1.0F)
            {
                l = 1.0F;
            }
            if (l < -1.0F)
            {
                l = -1.0F;
            }
            if (r > 1.0F)
            {
                r = 1.0F;
            }
            if (r < -1.0F)
            {
                r = -1.0F;
            }

            const uint32 outFrame = produced + f;
            if (bufferLayout == BufferLayout::Interleaved)
            {
                output[static_cast<uint64>(outFrame) * 2 + 0] = l;
                output[static_cast<uint64>(outFrame) * 2 + 1] = r;
            }
            else // Planar: [L...][R...]
            {
                output[outFrame] = l;
                output[static_cast<uint64>(frames) + outFrame] = r;
            }
        }

        RenderFrame += span;
        produced += span;
    }

    return Status::Ok;
}

void AudioSystem::Impl::FillSnapshot(SystemSnapshot& out) const noexcept
{
    SystemSnapshot& s = out;
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
