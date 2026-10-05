add_library(ludus_network_core STATIC)
add_library(Ludus::NetworkCore ALIAS ludus_network_core)
set_target_properties(ludus_network_core PROPERTIES EXPORT_NAME NetworkCore)
target_sources(ludus_network_core PRIVATE
    src/wire.cpp src/sequence.cpp src/send_queue.cpp src/link_simulator.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
    include/ludus/network/core/wire.h
    include/ludus/network/core/sequence.h
    include/ludus/network/core/send_queue.h
    include/ludus/network/core/link_simulator.h)
target_link_libraries(ludus_network_core PUBLIC Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_network_core)
install(TARGETS ludus_network_core EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_network_core_tests tests/network_tests.cpp)
    target_link_libraries(ludus_network_core_tests PRIVATE Ludus::NetworkCore Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_network_core_tests)
    ludus_enable_test_exceptions(ludus_network_core_tests)
    add_test(NAME ludus_network_core_tests COMMAND ludus_network_core_tests)
    set_tests_properties(ludus_network_core_tests PROPERTIES LABELS "unit;network")
endif()
