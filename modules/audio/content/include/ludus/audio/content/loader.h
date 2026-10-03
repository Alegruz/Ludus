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
// Exactly one control owner calls all methods. One acquisition is pending at a
// time; background work owns its input/results. Poll publishes a whole revision.
class Loader final
{
public:
    explicit Loader(AudioSystem& audio) noexcept;
    ~Loader() noexcept;
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;
    [[nodiscard]] ContentStatus Begin(std::string_view root,
                                      std::string_view id,
                                      const Sound* draft = nullptr,
                                      const Music* musicDraft = nullptr) noexcept;
    [[nodiscard]] ContentStatus Poll(const Routing& routing, Lease& output, Diagnostic& diagnostic) noexcept;
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
