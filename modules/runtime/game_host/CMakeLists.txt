# Ludus::GameHost — Qt-free static runtime library (design section 2).
#
# Owns engine lifecycles, the game window, the RHI session and ONE play session.
# Loads a project's native gameplay module (dlopen) or dispatches to a statically
# linked implementation. The editor never links this; a project-owned host
# executable and the static shipping executable both reuse this library's
# entry/lifecycle logic. No Qt dependency anywhere in its closure.

add_library(ludus_runtime_game_host STATIC)
add_library(Ludus::GameHost ALIAS ludus_runtime_game_host)

set_target_properties(ludus_runtime_game_host PROPERTIES
    OUTPUT_NAME ludus_runtime_game_host
    EXPORT_NAME GameHost
)

# Generate the host/module ABI identity header from ACTUAL build inputs. The
# string both host and module stamp and the host compares exactly before Create.
set(LUDUS_GAME_ABI_MAJOR 1)
set(LUDUS_GAME_ABI_MINOR 0)
if(NOT DEFINED LUDUS_TARGET_TRIPLE OR LUDUS_TARGET_TRIPLE STREQUAL "")
    set(LUDUS_TARGET_TRIPLE "${CMAKE_CXX_COMPILER_TARGET}")
endif()
if(LUDUS_TARGET_TRIPLE STREQUAL "")
    set(LUDUS_TARGET_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")
endif()
set(LUDUS_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(LUDUS_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")

set(LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/game_host_identity.hpp.in"
    "${LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR}/ludus/runtime/game_host/game_host_identity.hpp"
    @ONLY
    NEWLINE_STYLE UNIX
)

target_sources(ludus_runtime_game_host
    PRIVATE
        src/host.cpp
        src/host_services.cpp
        src/host_session.cpp
        src/identity.cpp
        src/module_loader.cpp
        src/protocol.cpp
        src/protocol_codec.cpp
        src/internal/host_services.h
        src/internal/host_session.h
        src/internal/identity.h
        src/internal/module_loader.h
        src/internal/protocol_codec.h
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES
            include/ludus/runtime/game_host/host.h
            include/ludus/runtime/game_host/protocol.h
)

target_include_directories(ludus_runtime_game_host
    PRIVATE
        src
        "${LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR}"
)

target_link_libraries(ludus_runtime_game_host
    PUBLIC
        Ludus::FoundationBase
        Ludus::GameApi
    PRIVATE
        Ludus::FoundationLogging
        ${CMAKE_DL_LIBS}
)

ludus_apply_project_defaults(ludus_runtime_game_host)

install(TARGETS ludus_runtime_game_host
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# L0 two-generation feasibility: build two tiny gameplay fixture modules (A and
# B) as MODULE libraries, plus a module that exports no entry and one with a
# wrong identity, then prove concurrent load, hidden symbols, correct code
# dispatch, and rejection of missing/incompatible modules.
if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    find_package(Threads REQUIRED)

    # Fixture module A and B share one source but differ by a compile define, so
    # each has its own code and the test can prove A and B run their own copy.
    foreach(variant A B)
        add_library(ludus_game_fixture_${variant} MODULE tests/fixture_module.cpp)
        target_link_libraries(ludus_game_fixture_${variant} PRIVATE Ludus::GameApi)
        target_compile_definitions(ludus_game_fixture_${variant} PRIVATE
            LUDUS_FIXTURE_VARIANT=${variant}
            LUDUS_FIXTURE_IDENTITY="${LUDUS_SDK_VARIANT}|${LUDUS_COMPILER_ID} ${LUDUS_COMPILER_VERSION}|${LUDUS_TARGET_TRIPLE}|abi-${LUDUS_GAME_ABI_MAJOR}.${LUDUS_GAME_ABI_MINOR}")
        ludus_apply_project_defaults(ludus_game_fixture_${variant})
        set_target_properties(ludus_game_fixture_${variant} PROPERTIES
            PREFIX "" OUTPUT_NAME "game_fixture_${variant}"
            C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    endforeach()

    # A module whose identity is deliberately wrong (rejected before Create).
    add_library(ludus_game_fixture_badid MODULE tests/fixture_module.cpp)
    target_link_libraries(ludus_game_fixture_badid PRIVATE Ludus::GameApi)
    target_compile_definitions(ludus_game_fixture_badid PRIVATE
        LUDUS_FIXTURE_VARIANT=A
        LUDUS_FIXTURE_IDENTITY="wrong-sdk-identity-should-be-rejected")
    ludus_apply_project_defaults(ludus_game_fixture_badid)
    set_target_properties(ludus_game_fixture_badid PROPERTIES
        PREFIX "" OUTPUT_NAME "game_fixture_badid"
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

    # A module that exports no entry symbol (EntryMissing).
    add_library(ludus_game_fixture_noentry MODULE tests/fixture_noentry.cpp)
    ludus_apply_project_defaults(ludus_game_fixture_noentry)
    set_target_properties(ludus_game_fixture_noentry PROPERTIES
        PREFIX "" OUTPUT_NAME "game_fixture_noentry"
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

    add_executable(ludus_game_host_loader_tests tests/loader_tests.cpp)
    target_include_directories(ludus_game_host_loader_tests PRIVATE src "${LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR}")
    target_link_libraries(ludus_game_host_loader_tests PRIVATE
        Ludus::GameHost Ludus::GameApi Ludus::FoundationLogging Catch2::Catch2WithMain ${CMAKE_DL_LIBS})
    ludus_apply_project_defaults(ludus_game_host_loader_tests)
    ludus_enable_test_exceptions(ludus_game_host_loader_tests)
    add_dependencies(ludus_game_host_loader_tests
        ludus_game_fixture_A ludus_game_fixture_B ludus_game_fixture_badid ludus_game_fixture_noentry)
    target_compile_definitions(ludus_game_host_loader_tests PRIVATE
        LUDUS_FIXTURE_A_PATH="$<TARGET_FILE:ludus_game_fixture_A>"
        LUDUS_FIXTURE_B_PATH="$<TARGET_FILE:ludus_game_fixture_B>"
        LUDUS_FIXTURE_BADID_PATH="$<TARGET_FILE:ludus_game_fixture_badid>"
        LUDUS_FIXTURE_NOENTRY_PATH="$<TARGET_FILE:ludus_game_fixture_noentry>")
    add_test(NAME ludus_game_host_loader_tests COMMAND ludus_game_host_loader_tests)
    set_tests_properties(ludus_game_host_loader_tests PROPERTIES TIMEOUT 120 LABELS "unit;runtime;loader")

    add_executable(ludus_game_host_protocol_tests tests/protocol_tests.cpp)
    target_include_directories(ludus_game_host_protocol_tests PRIVATE src)
    target_link_libraries(ludus_game_host_protocol_tests PRIVATE
        Ludus::GameHost Ludus::FoundationLogging Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_game_host_protocol_tests)
    ludus_enable_test_exceptions(ludus_game_host_protocol_tests)
    add_test(NAME ludus_game_host_protocol_tests COMMAND ludus_game_host_protocol_tests)
    set_tests_properties(ludus_game_host_protocol_tests PROPERTIES TIMEOUT 60 LABELS "unit;runtime;protocol")

    add_executable(ludus_game_host_session_tests tests/session_tests.cpp)
    target_include_directories(ludus_game_host_session_tests PRIVATE src "${LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR}")
    target_link_libraries(ludus_game_host_session_tests PRIVATE
        Ludus::GameHost Ludus::GameApi Ludus::FoundationLogging Catch2::Catch2WithMain Threads::Threads ${CMAKE_DL_LIBS})
    ludus_apply_project_defaults(ludus_game_host_session_tests)
    ludus_enable_test_exceptions(ludus_game_host_session_tests)
    add_dependencies(ludus_game_host_session_tests ludus_game_fixture_A ludus_game_fixture_B ludus_game_fixture_badid)
    target_compile_definitions(ludus_game_host_session_tests PRIVATE
        LUDUS_FIXTURE_A_PATH="$<TARGET_FILE:ludus_game_fixture_A>"
        LUDUS_FIXTURE_B_PATH="$<TARGET_FILE:ludus_game_fixture_B>"
        LUDUS_FIXTURE_BADID_PATH="$<TARGET_FILE:ludus_game_fixture_badid>")
    add_test(NAME ludus_game_host_session_tests COMMAND ludus_game_host_session_tests)
    set_tests_properties(ludus_game_host_session_tests PROPERTIES TIMEOUT 120 LABELS "unit;runtime;session;reload")

    add_executable(ludus_game_host_reload_tests tests/reload_tests.cpp)
    target_include_directories(ludus_game_host_reload_tests PRIVATE src "${LUDUS_GAME_HOST_GENERATED_INCLUDE_DIR}")
    target_link_libraries(ludus_game_host_reload_tests PRIVATE
        Ludus::GameHost Ludus::GameApi Ludus::FoundationLogging Catch2::Catch2WithMain ${CMAKE_DL_LIBS})
    ludus_apply_project_defaults(ludus_game_host_reload_tests)
    ludus_enable_test_exceptions(ludus_game_host_reload_tests)
    add_dependencies(ludus_game_host_reload_tests ludus_game_fixture_A ludus_game_fixture_B)
    target_compile_definitions(ludus_game_host_reload_tests PRIVATE
        LUDUS_FIXTURE_A_PATH="$<TARGET_FILE:ludus_game_fixture_A>"
        LUDUS_FIXTURE_B_PATH="$<TARGET_FILE:ludus_game_fixture_B>")
    add_test(NAME ludus_game_host_reload_tests COMMAND ludus_game_host_reload_tests)
    set_tests_properties(ludus_game_host_reload_tests PROPERTIES TIMEOUT 180 LABELS "unit;runtime;reload;stress")
endif()
