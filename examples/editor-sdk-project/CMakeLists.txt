cmake_minimum_required(VERSION 3.29)

# External installed-SDK sample for the Ludus editor workspace (requirement E13).
# It consumes the installed Ludus SDK via find_package only; no engine sources
# are compiled as part of this project's build. The matching SDK must already be
# installed and reachable through CMAKE_PREFIX_PATH (see CMakePresets.json, which
# supplies it from $env{LUDUS_SDK_PREFIX}).

project(LudusEditorSdkSample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Ludus CONFIG REQUIRED)

add_executable(sample_app main.cpp)
target_link_libraries(sample_app PRIVATE Ludus::FoundationBase)
