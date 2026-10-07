# Optional Qt Widgets editor workspace (native E0 and browser preview).
#
# This file is only entered when LUDUS_BUILD_EDITOR=ON (see the top-level
# CMakeLists.txt). All Qt discovery is local to this directory so a default
# (Editor OFF) native/browser configure never searches for Qt (E01). Qt headers
# stay in private app headers and .cpp files; no Editor target or header is added
# to the installed Ludus SDK export set.

# --- Native-host gating ------------------------------------------------------
# Native hosts share Qt Widgets and the owned POSIX process adapter.
if(NOT EMSCRIPTEN)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        # Linux reference host.
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|x86_64|amd64|AMD64)$")
        # Cocoa Qt plugin; Metal belongs to the separately launched game.
    else()
        message(FATAL_ERROR "The native Editor supports Linux x64 and macOS arm64/x64; browser builds require the pinned Qt WebAssembly toolchain.")
    endif()
endif()

if(EMSCRIPTEN)
    set(editor_process_sources src/browser_processes.cpp)
    set(editor_audio_source src/audio_preview_browser.cpp)
else()
    set(editor_process_sources src/tool_process.cpp src/play_process.cpp src/source_watch.cpp)
    set(editor_audio_source src/audio_preview.cpp)
endif()

# --- Qt discovery (local to the enabled editor only) -------------------------
# Use APIs available in Qt 6.4; require at least 6.4. Core/Gui/Widgets only, as
# native system libraries or the pinned source-built WebAssembly Qt.
# Qt is installed only by opted-in editor setup;
# it is never added to the engine Conan recipe or SDK.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

# QT_NO_KEYWORDS forbids the unprefixed signals/slots/emit macros so Qt's macro
# vocabulary cannot collide with engine/standard identifiers; use Q_SIGNALS/
# Q_SLOTS/Q_EMIT and the typed (pointer-to-member) connect overloads instead.
# AUTOMOC is enabled per target below, not globally.

# --- Shared editor logic library ---------------------------------------------
# The transition function, project store, tool-process protocol framing and log
# buffer live in a library so offscreen tests can exercise them without creating
# widgets or launching tools. All headers are private (src/internal).
add_library(ludus_editor_core STATIC
    src/internal/tool_process.h
    src/internal/play_process.h
    src/internal/source_watch.h
    src/workspace.cpp
    src/project_store.cpp
    src/recent_projects.cpp
    src/tool_operation.cpp
    ${editor_process_sources}
    src/log_buffer.cpp
)
set_target_properties(ludus_editor_core PROPERTIES AUTOMOC ON)
target_include_directories(ludus_editor_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ludus_editor_core PRIVATE QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_link_libraries(ludus_editor_core
    PUBLIC
        Qt6::Core
    PRIVATE
        Ludus::FoundationBase
        Ludus::FoundationLogging
        Ludus::AudioContent
)
ludus_apply_project_defaults(ludus_editor_core)

# --- Editor application ------------------------------------------------------
qt_add_executable(ludus_editor
    src/internal/controller.h
    src/internal/configuration_workspace.h
    src/internal/main_window.h
    src/internal/audio_workspace.h
    src/internal/audio_preview.h
    src/internal/script_workspace.h
    src/script_workspace.cpp
    src/configuration_workspace.cpp
    src/editor_files.cpp
    src/audio_workspace.cpp
    ${editor_audio_source}
    main.cpp
    src/main_window.cpp
    src/document_commands.cpp
    src/main_window_layout.cpp
    src/project_setup_dialog.cpp
    src/project_creation_dialog.cpp
    src/workspace_style.cpp
    src/controller.cpp
    src/controller_play.cpp
)
set_target_properties(ludus_editor PROPERTIES AUTOMOC ON)
if(APPLE)
    set_target_properties(ludus_editor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.ludus.editor"
        MACOSX_BUNDLE_BUNDLE_NAME "Ludus Editor")
endif()
target_include_directories(ludus_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ludus_editor PRIVATE QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_link_libraries(ludus_editor
    PRIVATE
        ludus_editor_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Ludus::FoundationBase
        Ludus::FoundationLogging
        Ludus::AudioContent
        Ludus::FoundationConfigJson
        Ludus::RuntimeConfiguration
)
ludus_apply_project_defaults(ludus_editor)

# The editor is a developer tool, never part of the installed SDK: no install()
# rule and no LudusTargets export entry here, by design (E01).

# --- Offscreen logic/widget tests --------------------------------------------
# Offscreen Qt tests verify logic and widget wiring without a GPU/compositor.
# They do NOT prove native Wayland interaction or that a game rendered a frame.
if(LUDUS_BUILD_TESTS AND NOT EMSCRIPTEN)
    find_package(Catch2 3 REQUIRED)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_executable(ludus_editor_tests
        src/internal/controller.h
        src/internal/configuration_workspace.h
        src/internal/main_window.h
        src/internal/audio_workspace.h
        src/internal/audio_preview.h
        src/internal/script_workspace.h
    src/script_workspace.cpp
        src/configuration_workspace.cpp
        src/editor_files.cpp
        src/audio_workspace.cpp
        ${editor_audio_source}
        tests/main.cpp
        tests/configuration_workspace_tests.cpp
        tests/audio_content_tests.cpp
        tests/workspace_tests.cpp
        tests/project_store_tests.cpp
        tests/project_store_v2_tests.cpp
        tests/tool_process_tests.cpp
        tests/log_buffer_tests.cpp
        tests/controller_tests.cpp
        tests/document_history_tests.cpp
        tests/document_interaction_tests.cpp
        tests/project_setup_dialog_tests.cpp
        tests/project_lifecycle_tests.cpp
        tests/workspace_layout_tests.cpp
        tests/recent_projects_tests.cpp
        tests/source_watch_tests.cpp
        tests/script_workspace_tests.cpp
        tests/live_journey_tests.cpp
        tests/macos_journey_tests.cpp
    )
    set_target_properties(ludus_editor_tests PROPERTIES AUTOMOC ON)
    target_include_directories(ludus_editor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/modules/audio/tests")
    target_compile_definitions(ludus_editor_tests PRIVATE QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII
        "LUDUS_EDITOR_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
        "LUDUS_EDITOR_PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\"")
    # Controller/MainWindow sources are compiled directly into the test binary so
    # tests can drive the controller and (offscreen) widgets.
    target_sources(ludus_editor_tests PRIVATE
        src/controller.cpp
        src/controller_play.cpp
        src/main_window.cpp
        src/document_commands.cpp
        src/main_window_layout.cpp
        src/project_setup_dialog.cpp
        src/project_creation_dialog.cpp
        src/workspace_style.cpp)
    target_link_libraries(ludus_editor_tests
        PRIVATE
            ludus_editor_core
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Test
            Ludus::FoundationBase
            Ludus::FoundationLogging
            Ludus::AudioContent
            Ludus::FoundationConfigJson
            Ludus::RuntimeConfiguration
            Catch2::Catch2)
    ludus_apply_project_defaults(ludus_editor_tests)
    # Catch2 reports assertion failures by throwing; test targets (only) re-enable
    # exceptions. Engine/app targets above stay exception-free (-fno-exceptions).
    ludus_enable_test_exceptions(ludus_editor_tests)
    # Print each completed case's duration to diagnose a stopped aggregate suite.
    add_test(NAME ludus_editor_tests COMMAND ludus_editor_tests --durations yes)
    # Run the offscreen Qt platform plugin so these logic/widget tests need no
    # display. This is for display-free tests only, never native acceptance.
    set_tests_properties(ludus_editor_tests PROPERTIES
        LABELS "unit;editor"
        TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(EMSCRIPTEN)
    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(ludus_editor PRIVATE Qt6::GuiPrivate)
    set_property(TARGET ludus_editor APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/browser/files.js" "${CMAKE_CURRENT_SOURCE_DIR}/browser/example.project.json")
    set_target_properties(ludus_editor PROPERTIES QT_WASM_INITIAL_MEMORY 33554432 QT_WASM_MAXIMUM_MEMORY 268435456)
    target_link_options(ludus_editor PRIVATE "-sASYNCIFY_STACK_SIZE=131072"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/browser/files.js"
        "--embed-file=${CMAKE_CURRENT_SOURCE_DIR}/browser/example.project.json@/browser-example/ludus.project.json")
endif()
