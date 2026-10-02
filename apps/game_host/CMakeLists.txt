# Reference GameHost executable (project-live-reload design 2). A project's own
# host target is built the same way via the LudusGame.cmake helper against its
# installed SDK; this in-tree target proves the entry/lifecycle path and backs
# the native acceptance journey. No Qt.

add_executable(ludus_game_host_app main.cpp)
set_target_properties(ludus_game_host_app PROPERTIES OUTPUT_NAME ludus_game_host)
target_link_libraries(ludus_game_host_app PRIVATE
    Ludus::GameHost Ludus::FoundationLogging)
ludus_apply_project_defaults(ludus_game_host_app)

# The host must dlopen the gameplay module with its own exported symbols
# available for RTLD_LOCAL modules that resolve host-provided weak references;
# export dynamic so the loader scope works even for a stripped install.
set_target_properties(ludus_game_host_app PROPERTIES ENABLE_EXPORTS ON)
