#pragma once

// The instance-owned Ludus audio control owner, per .kiro/specs/audio/design.md
// section 3 and requirements AU02-AU16.
//
// AudioSystem is a plain instance with explicit, caller-owned lifetime: no
// global service locator, no implicit thread-safe facade, no MPSC queue. Exactly
// one owner thread makes all public calls; jobs/game threads forward requests to
// that owner. Real-time submission/query methods are noexcept and allocate/free
// nothing. Loading and native device setup are explicit cold operations that may
// block the control owner; browser setup/shutdown are asynchronous.
//
// Construction is fallible: check IsValid() (or the Initialize status). Setup
// failure is reported explicitly, never through an exception (engine code is
// -fno-exceptions). No miniaudio / Web Audio type appears in this header.

#include <ludus/audio/audio_types.h>
#include <ludus/foundation/base/pointer.hpp> // ludus::foundation::core::UniquePtr
#include <ludus/foundation/base/types.h>

#include <span>

namespace ludus::audio
{
class AudioDebugSnapshotSink; // opt-in trace, declared in audio_debug.h

// Result of submitting a batch: the aggregate acceptance status plus the voice
// handles assigned to each Play record in submission order. A rejected batch
// yields no handles and mutates no accounting.
struct BatchResult final
{
    Status Result = Status::InvalidArgument;
    // Filled only on Status::Ok: one handle per Play command, in the order the
    // Play commands appeared in the batch. Non-Play commands contribute no entry.
    std::span<const VoiceHandle> Voices;
};

// Browser async teardown progress (design section 10).
enum class ShutdownState : uint8
{
    NotStarted,
    ClosingAdmission,
    QuiescingRenderer,
    QuiescingWorker,
    ClosingContext,
    Complete,
    Failed,
};

class AudioSystem final
{
public:
    AudioSystem() noexcept;
    ~AudioSystem() noexcept;

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    // --- Lifecycle (design section 3) -----------------------------------
    // Allocate fixed runtime storage, validate the bus tree / groups / capacity
    // products, and prepare the adapter for the configured mode. Fallible; a
    // failed Device init returns a failure and does not fall back to Disabled
    // (the caller chooses Disabled explicitly). Idempotent-safe: a second call
    // on an initialized system returns InvalidArgument.
    [[nodiscard]] Status Initialize(const SystemConfig& config) noexcept;

    [[nodiscard]] SystemState GetState() const noexcept;
    [[nodiscard]] Mode GetMode() const noexcept;
    [[nodiscard]] uint32 GetSession() const noexcept;
    [[nodiscard]] uint32 GetSampleRate() const noexcept;

    // --- Preparation (cold; design sections 3, 9) -----------------------
    // Decode/convert encoded bytes into owned finite PCM at the session rate.
    // The caller's bytes are borrowed only for the duration of this call.
    [[nodiscard]] Status
    PrepareClip(std::span<const uint8> encoded, const ClipDescriptor& descriptor, ClipHandle& outClip) noexcept;

    // Open/validate a seekable stream source off rendering and prefill chunks.
    // `source` is an owned byte reader whose lifetime the worker takes over.
    [[nodiscard]] Status
    PrepareStream(std::span<const uint8> encoded, const StreamDescriptor& descriptor, StreamHandle& outStream) noexcept;

    // Close new admission for an asset; storage is retained for existing
    // reservations/voices/worker jobs and reclaimed by Service after terminal
    // acknowledgment. No forced stop.
    [[nodiscard]] Status RetireClip(ClipHandle clip) noexcept;
    [[nodiscard]] Status RetireStream(StreamHandle stream) noexcept;

    // --- Submission (warm; design section 4) ----------------------------
    // Validate the whole batch (<= MAX_BATCH_RECORDS), copy all records, reserve
    // slots and asset pins transactionally, then publish once. The returned
    // Voices span is owned by the system and remains valid until the next
    // submission call. noexcept, no allocation, no lock, no wait.
    [[nodiscard]] BatchResult TrySubmitBatch(std::span<const Command> commands) noexcept;

    // Convenience single-command Play using the batch path.
    [[nodiscard]] Status PlayClip(const PlayParams& params, VoiceHandle& outVoice) noexcept;

    // Stream playback: a prepared StreamHandle is admitted once. A second play
    // on the same handle returns NotReady.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): accepted public shape.
    [[nodiscard]] Status
    PlayStream(StreamHandle stream, uint32 busIndex, uint8 priority, float32 gain, VoiceHandle& outVoice) noexcept;

    // --- Cancellation (always available; design section 4) --------------
    // Stop uses a per-slot generation mailbox independent of normal queue room.
    // Idempotent for a terminal handle still known to the owner.
    [[nodiscard]] Status Stop(VoiceHandle voice) noexcept;

    // Advance the cancellation epoch; affects all voices admitted before this
    // call, including queued plays. Never needs queue space. Mix settings remain.
    void StopAll() noexcept;

    // --- Service (owner/control thread; design section 3) ---------------
    // Drain diagnostics, acknowledge durable terminals, free retired storage,
    // and advance setup/worker/backend lifecycle. Not a mixer tick. Output
    // continuity does not depend on calling this at the game frame rate.
    void Service() noexcept;

    // --- Queries (design sections 3, 11) --------------------------------
    [[nodiscard]] Status GetVoiceInfo(VoiceHandle voice, VoiceInfo& out) const noexcept;
    [[nodiscard]] Status GetGroupInfo(uint32 groupIndex, GroupInfo& out) const noexcept;
    [[nodiscard]] Status GetBusMeter(uint32 busIndex, BusMeter& out) const noexcept;
    void GetSystemSnapshot(SystemSnapshot& out) const noexcept;

    // --- Offline rendering (Offline mode only; design section 5) --------
    // Run the production render entry serially on the caller thread. Rejects
    // concurrent device rendering. `output` is validated against layout/frames.
    [[nodiscard]] Status
    RenderOffline(std::span<float32> output, ChannelLayout layout, BufferLayout bufferLayout, uint32 frames) noexcept;

    // --- Browser gesture / shutdown (design section 10) -----------------
    // Must be called inside a genuine DOM activation handler on the browser.
    // Schedules resume; returns Pending/result.
    [[nodiscard]] Status ResumeFromUserGesture() noexcept;

    // Close admission, quiesce renderer/worker, reclaim after proof. Native may
    // finish synchronously; browser remains alive until async completion.
    [[nodiscard]] Status BeginShutdown() noexcept;
    [[nodiscard]] ShutdownState GetShutdownState() const noexcept;

    // --- Diagnostics opt-in ---------------------------------------------
    // Attach/detach an owner-side snapshot/trace sink. Does not transfer
    // ownership; the sink must outlive the attachment. Null detaches.
    void SetDebugSink(AudioDebugSnapshotSink* sink) noexcept;

    // --- Attenuation modifiers (design section 8) -----------------------
    // Acquire a bounded attenuation-modifier instance with per-bus dB targets
    // and a weight in [0,1]. Returns a generation-checked ModifierHandle.
    // Overlapping owners get distinct instances. AssetCapacity-style failure is
    // reported as Status (no handle). The modifier ramps in on acquisition.
    [[nodiscard]] Status
    AcquireModifier(std::span<const ModifierValue> values, float32 weight, ModifierHandle& outModifier) noexcept;

    // Update a modifier's weight (ramps). Generation-checked.
    [[nodiscard]] Status UpdateModifier(ModifierHandle modifier, float32 weight) noexcept;

    // Release a modifier: fades its weight to zero, then retires its slot after
    // the renderer acknowledges. Removing one instance never removes another.
    [[nodiscard]] Status ReleaseModifier(ModifierHandle modifier) noexcept;

    // --- Owner-side user-gain helper (design section 8) -----------------
    // Map a normalized slider u in [0,1] through an explicit dB range to a
    // UserGain amplitude: 0 at u==0 (exact mute), else 10^((u-1)*range/20).
    [[nodiscard]] static float32 UserGainFromSlider(float32 u, float32 dbRange) noexcept;

    // True when construction + Initialize produced usable fixed storage.
    [[nodiscard]] bool IsValid() const noexcept
    {
        return mImpl.Get() != nullptr;
    }

private:
    struct Impl;
    ludus::foundation::core::UniquePtr<Impl> mImpl;
};

} // namespace ludus::audio
