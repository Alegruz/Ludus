add_library(ludus_gameplay_world STATIC src/entity_registry.cpp)
add_library(Ludus::GameplayWorld ALIAS ludus_gameplay_world)
set_target_properties(ludus_gameplay_world PROPERTIES EXPORT_NAME GameplayWorld)
target_sources(ludus_gameplay_world PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
    include/ludus/gameplay/world/entity_id.h
    include/ludus/gameplay/world/entity_registry.h
    include/ludus/gameplay/world/component_pool.hpp)
target_link_libraries(ludus_gameplay_world PUBLIC Ludus::FoundationBase Ludus::FoundationContainers)
ludus_apply_project_defaults(ludus_gameplay_world)
install(TARGETS ludus_gameplay_world EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_gameplay_world_tests tests/world_tests.cpp)
    target_link_libraries(ludus_gameplay_world_tests PRIVATE Ludus::GameplayWorld Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_gameplay_world_tests)
    ludus_enable_test_exceptions(ludus_gameplay_world_tests)
    add_test(NAME ludus_gameplay_world_tests COMMAND ludus_gameplay_world_tests)
    set_tests_properties(ludus_gameplay_world_tests PROPERTIES LABELS "unit;world")
endif()
