// Project-owned entry; installed by ludus_add_game. The runtime owns parsing,
// diagnostics, presentation and cleanup for both dynamic and static dispatch.
#include <ludus/runtime/game_host/host.h>

int main(int argc, char** argv)
{
    return ludus::runtime::game_host::RunMain(argc, argv);
}
