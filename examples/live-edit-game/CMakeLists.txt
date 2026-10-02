# External gameplay project consuming the INSTALLED Ludus SDK (project-live-reload
# design 2). This directory is NOT part of the engine build; it is configured
# standalone against an installed SDK (find_package(Ludus CONFIG REQUIRED)), which
# proves a module rebuild compiles no engine source. ludus_add_game builds the
# reloadable module, the project host, and the static shipping executable from the
# single game.cpp implementation.

cmake_minimum_required(VERSION 3.29)
project(ludus_live_edit_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Ludus CONFIG REQUIRED)

ludus_add_game(
    NAME sample_game
    SOURCES src/game.cpp
)
