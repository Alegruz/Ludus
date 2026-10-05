# External gameplay project consuming the INSTALLED Ludus SDK (project-live-reload
# design 2). This directory is NOT part of the engine build; it is configured
# standalone against an installed SDK (find_package(Ludus CONFIG REQUIRED)), which
# proves a module rebuild compiles no engine source. ludus_add_game builds the
# reloadable module, the project host, and the static shipping executable from the
# single game.cpp implementation.

cmake_minimum_required(VERSION 3.29)
project(ludus_live_edit_game LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Ludus CONFIG REQUIRED)

ludus_generate_reflection(TARGET sample_game_reflection SCHEMA body.schema.json BASELINE body.schema.baseline.json)

ludus_add_game(
    NAME sample_game
    SOURCES src/game.cpp
)

foreach(target sample_game_module sample_game_shipping)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE sample_game_reflection Ludus::GamePropertyBinding)
    endif()
endforeach()

if(BUILD_TESTING)
    add_test(NAME sample_game_headless_smoke
        COMMAND $<TARGET_FILE:sample_game_shipping> --headless --max-frames 2)
    set_tests_properties(sample_game_headless_smoke PROPERTIES TIMEOUT 20)
endif()
