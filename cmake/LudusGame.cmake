# LudusGame.cmake — helper for a project that consumes the installed Ludus SDK
# to build a gameplay module plus its host and shipping executables
# (project-live-reload design section 2). Installed with the SDK and included by
# LudusConfig.cmake, so a project does `find_package(Ludus CONFIG REQUIRED)` and
# then calls ludus_add_game(...).
#
# The same game source has ONE implementation. ludus_add_game builds:
#   <name>_module   — a MODULE library the dev host dlopens (reloadable).
#   <name>_host     — a project-owned host executable (links Ludus::GameHost).
#   <name>_shipping — a static executable linking the game sources directly
#                     (selecting static dispatch replaces only the module lookup).
#
# A gameplay module links ONLY Ludus::GameApi (no engine singleton runtime); the
# host and shipping executables link Ludus::GameHost. The helper stamps hidden
# visibility and exactly one exported entry on the module, matching the loader's
# RTLD_LOCAL contract.

include_guard(GLOBAL)

function(ludus_add_game)
    set(options "")
    set(oneValue NAME)
    set(multiValue SOURCES)
    cmake_parse_arguments(GAME "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    if(NOT GAME_NAME)
        message(FATAL_ERROR "ludus_add_game: NAME is required")
    endif()
    if(NOT GAME_SOURCES)
        message(FATAL_ERROR "ludus_add_game: SOURCES is required")
    endif()

    # --- Reloadable gameplay module (dev) ---
    add_library(${GAME_NAME}_module MODULE ${GAME_SOURCES})
    target_link_libraries(${GAME_NAME}_module PRIVATE Ludus::GameApi)
    if(DEFINED Ludus_SDK_IDENTITY)
        target_compile_definitions(${GAME_NAME}_module PRIVATE LUDUS_EXAMPLE_IDENTITY="${Ludus_SDK_IDENTITY}")
    endif()
    set_target_properties(${GAME_NAME}_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${GAME_NAME}"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

    # --- Project-owned host executable ---
    if(TARGET Ludus::GameHost)
        add_executable(${GAME_NAME}_host "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LudusGameHostMain.cpp")
        target_link_libraries(${GAME_NAME}_host PRIVATE Ludus::GameHost)
        set_target_properties(${GAME_NAME}_host PROPERTIES
            OUTPUT_NAME "${GAME_NAME}_host"
            ENABLE_EXPORTS ON)

        # --- Static shipping executable: same game sources linked directly ---
        add_executable(${GAME_NAME}_shipping
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LudusGameShippingMain.cpp"
            ${GAME_SOURCES})
        target_link_libraries(${GAME_NAME}_shipping PRIVATE Ludus::GameHost Ludus::GameApi)
        target_compile_definitions(${GAME_NAME}_shipping PRIVATE LUDUS_GAME_STATIC_DISPATCH=1)
        if(DEFINED Ludus_SDK_IDENTITY)
            target_compile_definitions(${GAME_NAME}_shipping PRIVATE LUDUS_EXAMPLE_IDENTITY="${Ludus_SDK_IDENTITY}")
        endif()
        set_target_properties(${GAME_NAME}_shipping PROPERTIES OUTPUT_NAME "${GAME_NAME}")
    endif()
endfunction()
