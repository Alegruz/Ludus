#include "presentation.h"
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <ludus/foundation/math/scalar.hpp>

#include <string_view>
#include <time.h>

namespace
{
using namespace sample;
bool Acquire(audio::content::Loader& loader,
             std::string_view root,
             std::string_view id,
             const audio::content::Routing& routing,
             audio::content::Lease& lease) noexcept
{
    if (loader.Begin(root, id) != ludus::content::Status::Pending)
    {
        return false;
    }
    for (uint32 i = 0; i < 10000; ++i)
    {
        ludus::content::Diagnostic diagnostic;
        const auto status = loader.Poll(routing, lease, diagnostic);
        if (status != ludus::content::Status::Pending)
        {
            return status == ludus::content::Status::Ok;
        }
        const timespec wait{0, 1000000};
        nanosleep(&wait, nullptr);
    }
    loader.Cancel();
    return false;
}
bool Run(std::string_view root, bool native) noexcept
{
    audio::BusConfig buses[3];
    buses[0].ParentIndex = audio::kNoParent;
    buses[1].ParentIndex = 0;
    buses[2].ParentIndex = 0;
    audio::GroupConfig groups[2];
    const std::string_view names[] = {"master", "sfx", "music"}, groupNames[] = {"default", "impacts"};
    const audio::content::Routing routing{names, groupNames};
    audio::SystemConfig config;
    config.SystemMode = native ? audio::Mode::Device : audio::Mode::Offline;
    config.Buses = buses;
    config.Groups = groups;
    audio::AudioSystem system;
    const auto initialized = system.Initialize(config);
    if (initialized != audio::Status::Ok)
    {
        LUDUS_LOG_ERROR(ludus::foundation::logging::LOG_CORE, "Audio initialization: {}", audio::ToString(initialized));
        return false;
    }
    audio::content::Loader loader(system);
    audio::content::Lease soundLease, musicLease;
    if (!Acquire(loader, root, "sound/impact", routing, soundLease) ||
        !Acquire(loader, root, "music/theme", routing, musicLease))
    {
        return false;
    }
    audio::content::Sound sound;
    audio::app::EventDescriptor descriptor;
    if (loader.GetSound(soundLease, sound, descriptor) != ludus::content::Status::Ok)
    {
        return false;
    }
    audio::VoiceHandle musicVoice;
    if (loader.PlayMusic(musicLease, musicVoice) != audio::Status::Ok)
    {
        return false;
    }
    // The session owns music separately from world-owned loops. A failed incoming
    // acquisition cannot release musicLease or stop musicVoice.
    audio::content::Lease broken;
    if (Acquire(loader, root, "music/missing", routing, broken))
    {
        return false;
    }
    Presentation presentation(system);
    presentation.ChangeWorld(1);
    Request request;
    request.Sequence = 1;
    request.Entity = {1, 0, 1};
    request.Seed = 17;
    request.Required = true;
    request.SoundId = sound.Id;
    if (presentation.Deliver(request, 0, sound, descriptor) != audio::Status::Ok ||
        presentation.Deliver(request, 0, sound, descriptor) != audio::Status::Ok)
    {
        return false;
    }
    audio::SystemSnapshot snapshot;
    system.GetSystemSnapshot(snapshot);
    if (snapshot.LogicalHighWater != 2)
    {
        return false;
    }
    float32 output[1024];
    float32 peak = 0;
    for (uint32 tick = 0; tick < (native ? 600 : 120); ++tick)
    {
        if (tick == 10 && presentation.SetPaused(true) != audio::Status::Ok)
        {
            return false;
        }
        if (tick == 20 && presentation.SetPaused(false) != audio::Status::Ok)
        {
            return false;
        }
        if (tick == 30)
        {
            presentation.ChangeWorld(2);
            request.Sequence = 2;
            request.Entity.World = 1;
            if (presentation.Deliver(request, tick, sound, descriptor) != audio::Status::Ok)
            {
                return false;
            }
        }
        if (tick == 90)
        {
            audio::VoiceHandle incoming;
            if (loader.PlayMusic(musicLease, incoming) != audio::Status::Ok)
            {
                return false;
            }
            (void)system.Stop(musicVoice);
            musicVoice = incoming;
        }
        if (tick == 60)
        {
            request.Sequence = 3;
            request.Entity = {2, 0, 2};
            if (presentation.Deliver(request, tick, sound, descriptor) != audio::Status::Ok)
            {
                return false;
            }
        }
        if (!native)
        {
            if (system.RenderOffline(output, audio::ChannelLayout::Stereo, audio::BufferLayout::Interleaved, 512) !=
                audio::Status::Ok)
            {
                return false;
            }
            for (const auto sample : output)
            {
                if (!ludus::foundation::math::IsFinite(sample))
                {
                    return false;
                }
                const auto level = ludus::foundation::math::Abs(sample);
                if (level > peak)
                {
                    peak = level;
                }
            }
        }
        presentation.Service(tick);
        loader.Service();
        const timespec wait{0, native ? 16666667L : 1000000L};
        nanosleep(&wait, nullptr);
    }
    if (!native && peak < 0.05F)
    {
        return false;
    }
    system.GetSystemSnapshot(snapshot);
    LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                   "Runtime memory: resident={} encoded={} rings={}; starvations={} errors={}",
                   snapshot.ResidentPcmBytes,
                   snapshot.StreamEncodedBytes,
                   snapshot.StreamRingBytes,
                   snapshot.StreamStarvations,
                   snapshot.Errors);
    (void)system.Stop(musicVoice);
    system.StopAll();
    if (loader.Release(soundLease) != ludus::content::Status::Ok ||
        loader.Release(musicLease) != ludus::content::Status::Ok)
    {
        return false;
    }
    if (system.BeginShutdown() != audio::Status::Ok)
    {
        return false;
    }
    LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                   "Audio content demo passed (native={}, offline peak={})",
                   native,
                   peak);
    return true;
}
} // namespace
int main(int argc, char** argv)
{
    ludus::foundation::logging::LogConfig log;
    log.EnableConsole = true;
    log.EnableDebugger = false;
    log.EnableFile = false;
    ludus::foundation::logging::LogSystem::Initialize(log);
    struct LoggerCleanup final
    {
        ~LoggerCleanup() noexcept
        {
            ludus::foundation::logging::LogSystem::Shutdown();
        }
    } cleanup;
    if (argc != 3 || (std::string_view(argv[1]) != "--offline" && std::string_view(argv[1]) != "--native"))
    {
        return 2;
    }
    return Run(argv[2], std::string_view(argv[1]) == "--native") ? 0 : 1;
}
