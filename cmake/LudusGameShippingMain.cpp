// Static shipping entry generated for a project by ludus_add_game (design 2).
// The game's LudusGetGameApi is linked into this binary; the host runs it with
// no dlopen and no reload. The same game source produces this and the module.

#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_host/host.h>

#include <ludus/foundation/base/types.h>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    ludus::runtime::game_host::HostConfig config;
    config.Source = ludus::runtime::game_host::GameplaySource::Static;
    config.Mode = ludus::runtime::game_host::Presentation::Windowed;
    config.MaxFrames = 0;

    // Reinterpret the project's LudusGetGameApi to the host's static entry type.
    auto entry = reinterpret_cast<ludus::runtime::game_host::StaticEntryFn>(&LudusGetGameApi);
    const ludus::runtime::game_host::RunResult result = ludus::runtime::game_host::RunStatic(config, entry);
    return static_cast<int>(result);
}
