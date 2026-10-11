#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/content/content.h>

#include <ludus/audio/audio_app_adapter.h>
#include <ludus/audio/audio_system.h>
#include <ludus/audio/content/definitions.h>
#include <ludus/audio/content/loader.h>
#include <ludus/runtime/resources/manager.h>

namespace sample
{
// Private composition adapter: one pending/resident sound at a time. Its audio
// system must have no other resident-clip owners (music streams are independent).
// This deliberately conservative boundary can acknowledge even partial failed
// installation by waiting for the system's resident PCM count to reach zero.
// Borrowed root/catalog/routing must outlive the adapter and remain immutable.
class SoundResourceAdapter final
{
public:
    struct Payload final
    {
        ludus::audio::content::Sound Definition;
        ludus::audio::app::EventDescriptor Descriptor;
    };
    SoundResourceAdapter(ludus::audio::AudioSystem& system,
                         const ludus::content::Catalog& catalog,
                         const ludus::content::Text<4096>& root,
                         const ludus::audio::content::Routing& routing) noexcept;
    [[nodiscard]] ludus::resources::Adapter Descriptor() noexcept;
    // Cold application destruction only, after Manager::Shutdown closes demand.
    // Join pending file/decode access before destroying stack-owned contexts.
    void CancelAndJoin() noexcept;

private:
    ludus::audio::AudioSystem& mSystem;
    const ludus::content::Catalog& mCatalog;
    const ludus::content::Text<4096>& mRoot;
    const ludus::audio::content::Routing& mRouting;
    ludus::audio::content::Loader mLoader;
    bool mUsed = false;
    bool mPending = false;
    bool mReleased = false;
    ludus::audio::content::Lease mLease;
    Payload mPayload;
};
} // namespace sample
