// Static shipping uses exactly the same host entry and game source.
#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_host/host.h>

int main(int argc, char** argv)
{
    return ludus::runtime::game_host::RunMain(argc, argv, &LudusGetGameApi);
}
