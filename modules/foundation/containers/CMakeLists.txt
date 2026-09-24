add_library(ludus_foundation_containers STATIC)
add_library(Ludus::FoundationContainers ALIAS ludus_foundation_containers)

set_target_properties(
    ludus_foundation_containers
    PROPERTIES
        OUTPUT_NAME ludus_foundation_containers
        EXPORT_NAME FoundationContainers
)

target_sources(
    ludus_foundation_containers
    PRIVATE
        src/vector_support.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            include
        FILES
            include/ludus/foundation/containers/array.hpp
            include/ludus/foundation/containers/vector.hpp
            include/ludus/foundation/containers/relocation.hpp
            include/ludus/foundation/containers/containers.hpp
            include/ludus/foundation/containers/detail/contiguous_storage.hpp
)

# Dependency direction: FoundationContainers may use FoundationBase (types,
# assertions, compiler macros), never the reverse.
target_link_libraries(
    ludus_foundation_containers
    PUBLIC
        Ludus::FoundationBase
)

ludus_apply_project_defaults(ludus_foundation_containers)

install(
    TARGETS ludus_foundation_containers
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)

    add_executable(ludus_foundation_containers_tests)
    target_sources(
        ludus_foundation_containers_tests
        PRIVATE
            tests/array_tests.cpp
            tests/vector_tests.cpp
            tests/relocation_tests.cpp
    )
    target_include_directories(ludus_foundation_containers_tests PRIVATE tests)
    target_link_libraries(
        ludus_foundation_containers_tests
        PRIVATE
            Ludus::FoundationContainers
            Catch2::Catch2WithMain
    )
    ludus_apply_project_defaults(ludus_foundation_containers_tests)
    ludus_enable_test_exceptions(ludus_foundation_containers_tests)

    add_test(NAME ludus_foundation_containers_tests COMMAND ludus_foundation_containers_tests)
    set_tests_properties(ludus_foundation_containers_tests PROPERTIES LABELS "unit;foundation;containers")

    # Zero-allocation / allocation-count assertions reuse the malloc-wrap pattern
    # used by the assertion tests: the container's operator-new path is observed
    # by wrapping the C allocator underneath. Runs only when not sanitized (the
    # sanitizer runtime intercepts these symbols itself).
    if(NOT LUDUS_ENABLE_ASAN AND NOT LUDUS_ENABLE_UBSAN AND NOT LUDUS_ENABLE_TSAN)
        add_executable(ludus_containers_alloc_tests)
        target_sources(ludus_containers_alloc_tests PRIVATE tests/allocation_tests.cpp)
        target_include_directories(ludus_containers_alloc_tests PRIVATE tests)
        target_link_libraries(
            ludus_containers_alloc_tests
            PRIVATE
                Ludus::FoundationContainers
                Catch2::Catch2WithMain
        )
        ludus_apply_project_defaults(ludus_containers_alloc_tests)
        ludus_enable_test_exceptions(ludus_containers_alloc_tests)
        add_test(NAME ludus_containers_alloc_tests COMMAND ludus_containers_alloc_tests)
        set_tests_properties(ludus_containers_alloc_tests PROPERTIES LABELS "unit;foundation;containers;alloc")
    endif()
endif()
