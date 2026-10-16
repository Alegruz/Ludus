# G0 curves and surfaces reference corpus (ADR 0024). Test-only: never installed
# or exported. Consumers include <curves_surfaces/reference_fixtures.hpp>.
add_library(ludus_geometry_reference_fixtures INTERFACE)
target_include_directories(ludus_geometry_reference_fixtures INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(
    ludus_geometry_reference_fixtures
    INTERFACE
        Ludus::FoundationBase
        Ludus::FoundationContainers
)

# Validates the generator's oracles against closed forms and exact identities,
# and rejects a committed header that no longer matches the generator.
add_test(NAME ludus_geometry_reference_fixtures
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_fixtures.py -v)
set_tests_properties(ludus_geometry_reference_fixtures PROPERTIES LABELS "unit;fixtures;geometry" TIMEOUT 600)
