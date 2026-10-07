#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/audio/audio_system.h>
#include <ludus/audio/content/definitions.h>

#include <string_view>

namespace ludus::audio::content
{
struct Lease final
{
    uint32 Slot = 0;
    uint32 Generation = 0;
    [[nodiscard]] bool operator==(const Lease&) const noexcept = default;
};
/// Acquire sound/music revisions on a Linux/macOS background worker.
/// Exactly one control owner calls all methods; at most one acquisition is pending.
/// Background work owns inputs/results, and Poll installs a whole revision.
/// @note The referenced AudioSystem must outlive this loader. Destroy the loader
/// on its control owner; destruction joins pending acquisition before reclaiming it.
class Loader final
{
public:
    explicit Loader(AudioSystem& audio) noexcept;
    ~Loader() noexcept;
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;
    /// Begin cold file acquisition and decoding without blocking the control owner.
    /// @param root Native content root copied into the pending job.
    /// @param id Catalog resource identifier copied into the pending job.
    /// @param draft Optional sound draft copied and validated before worker launch.
    /// @param musicDraft Optional music draft; mutually exclusive with draft.
    /// @return Pending after launch or while an acquisition is already pending;
    /// Invalid for invalid input/uninitialized audio; OutOfMemory for allocation
    /// failure; IoError for worker creation failure. Other platforms are Unsupported.
    [[nodiscard]] ContentStatus Begin(std::string_view root,
                                      std::string_view id,
                                      const Sound* draft = nullptr,
                                      const Music* musicDraft = nullptr) noexcept;
    /// Install completed acquisition on the control owner after routing checks.
    /// @param routing Bus/group names borrowed during this call.
    /// @param output Receives the new lease on Ok; preserved on failure/Pending.
    /// @param diagnostic Receives completed acquisition/validation diagnostics.
    /// @return Pending while the worker runs; Ok on installation; NotFound with
    /// no pending job; Cancelled after the audio session changes or stops; otherwise
    /// the acquisition/validation status. A completed job is joined before release.
    [[nodiscard]] ContentStatus Poll(const Routing& routing, Lease& output, Diagnostic& diagnostic) noexcept;
    /// Cancel and join pending acquisition on the control owner before freeing
    /// its inputs. May block until current file/decode work returns; no pending
    /// acquisition is a no-op. Existing revision leases remain valid.
    void Cancel() noexcept;
    [[nodiscard]] ContentStatus
    GetSound(Lease lease, Sound& definition, app::EventDescriptor& descriptor) const noexcept;
    [[nodiscard]] ContentStatus GetMusic(Lease lease, Music& definition) const noexcept;
    [[nodiscard]] Status PlayMusic(Lease lease, VoiceHandle& voice) noexcept;
    [[nodiscard]] ContentStatus Release(Lease lease) noexcept;
    void Service() noexcept;

private:
    struct Impl;
    Impl* mImpl = nullptr;
};
} // namespace ludus::audio::content
