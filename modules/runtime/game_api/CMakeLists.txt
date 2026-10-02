# Ludus::GameApi — the installed gameplay ABI contract (design section 2).
#
# This is a small INTERFACE target: Band-0 types, ABI declarations and
# inexpensive wrappers only. It links no engine implementation, so a gameplay
# module compiled against it pulls in NO engine singleton runtime. Only the
# public ABI headers are installed/exported.

add_library(ludus_runtime_game_api INTERFACE)
add_library(Ludus::GameApi ALIAS ludus_runtime_game_api)

set_target_properties(ludus_runtime_game_api PROPERTIES
    EXPORT_NAME GameApi
)

target_sources(ludus_runtime_game_api
    INTERFACE
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES
            include/ludus/runtime/game_api/abi.h
            include/ludus/runtime/game_api/services.h
            include/ludus/runtime/game_api/api.h
            include/ludus/runtime/game_api/properties.h
)

# The ABI headers need only the fixed-width aliases. FoundationBase is a
# header-only Band-0 dependency; no higher-level engine module is pulled in.
target_link_libraries(ludus_runtime_game_api INTERFACE Ludus::FoundationBase)

install(TARGETS ludus_runtime_game_api
    EXPORT LudusTargets
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Header self-sufficiency / foundational-include gate: compile each public ABI
# header on its own as a tiny TU so a consumer that includes only one header
# still builds. This also proves the no-exceptions / warning-clean contract on
# the installed surface (requirement L04/L15).
if(LUDUS_BUILD_TESTS)
    add_library(ludus_runtime_game_api_header_check STATIC
        tests/header_check_abi.cpp
        tests/header_check_services.cpp
        tests/header_check_api.cpp
        tests/header_check_properties.cpp
    )
    target_link_libraries(ludus_runtime_game_api_header_check PRIVATE Ludus::GameApi)
    ludus_apply_project_defaults(ludus_runtime_game_api_header_check)

    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_runtime_game_api_tests tests/abi_layout_tests.cpp)
    target_link_libraries(ludus_runtime_game_api_tests PRIVATE Ludus::GameApi Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_runtime_game_api_tests)
    ludus_enable_test_exceptions(ludus_runtime_game_api_tests)
    add_test(NAME ludus_runtime_game_api_tests COMMAND ludus_runtime_game_api_tests)
    set_tests_properties(ludus_runtime_game_api_tests PROPERTIES TIMEOUT 60 LABELS "unit;runtime;abi")
endif()
