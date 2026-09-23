add_library(ludus_foundation_logging STATIC)
add_library(Ludus::FoundationLogging ALIAS ludus_foundation_logging)

set_target_properties(
    ludus_foundation_logging
    PROPERTIES
        OUTPUT_NAME ludus_foundation_logging
        EXPORT_NAME FoundationLogging
)

target_sources(
    ludus_foundation_logging
    PRIVATE
        src/logger.cpp
        src/formatter.cpp
        src/emergency_logger.cpp
        src/sinks/console_sink.cpp
        src/sinks/file_sink.cpp
        src/sinks/debugger_sink.cpp
        src/internal/log_record.hpp
        src/internal/sink.hpp
        src/internal/emergency_logger.hpp
        src/internal/formatter.hpp
        src/sinks/console_sink.hpp
        src/sinks/file_sink.hpp
        src/sinks/debugger_sink.hpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            include
        FILES
            include/ludus/foundation/logging/log.hpp
            include/ludus/foundation/logging/level.hpp
            include/ludus/foundation/logging/category.hpp
            include/ludus/foundation/logging/config.hpp
)

# Private include root so translation units can reference "internal/..." and
# "sinks/..." headers, matching the FoundationBase convention.
target_include_directories(
    ludus_foundation_logging
    PRIVATE
        src
)

# Public dependency direction (spec section 2.1): FoundationLogging may use
# FoundationBase, never the reverse.
target_link_libraries(
    ludus_foundation_logging
    PUBLIC
        Ludus::FoundationBase
)

# Compile-time minimum level. LUDUS_COMPILED_LOG_LEVEL selects the lowest level
# that survives compilation; lower levels become ((void)0) and their arguments
# are never evaluated (spec sections 7, 30). Configurable via cache variable and
# defaulted per build type here.
if(NOT DEFINED LUDUS_COMPILED_LOG_LEVEL)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(LUDUS_COMPILED_LOG_LEVEL 0) # Trace+
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(LUDUS_COMPILED_LOG_LEVEL 3) # Warning+
    elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        set(LUDUS_COMPILED_LOG_LEVEL 3) # Warning+
    else()
        # RelWithDebInfo covers both Development (Debug+) and Profile (Info+).
        # Default to Debug+ so Development keeps rich logs; a Profile preset can
        # override this cache variable to 2 (Info+).
        set(LUDUS_COMPILED_LOG_LEVEL 1) # Debug+
    endif()
endif()

set(LUDUS_COMPILED_LOG_LEVEL "${LUDUS_COMPILED_LOG_LEVEL}" CACHE STRING
    "Lowest log level compiled in: 0=Trace 1=Debug 2=Info 3=Warning 4=Error 5=Fatal")

target_compile_definitions(
    ludus_foundation_logging
    PUBLIC
        LUDUS_COMPILED_LOG_LEVEL=${LUDUS_COMPILED_LOG_LEVEL}
)

ludus_apply_project_defaults(ludus_foundation_logging)

install(
    TARGETS ludus_foundation_logging
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)

    add_executable(ludus_foundation_logging_tests)
    target_sources(
        ludus_foundation_logging_tests
        PRIVATE
            tests/logging_tests.cpp
            tests/category_tests.cpp
            tests/formatting_tests.cpp
    )
    target_link_libraries(
        ludus_foundation_logging_tests
        PRIVATE
            Ludus::FoundationLogging
            Catch2::Catch2WithMain
    )
    ludus_apply_project_defaults(ludus_foundation_logging_tests)
    ludus_enable_test_exceptions(ludus_foundation_logging_tests)

    add_test(NAME ludus_foundation_logging_tests COMMAND ludus_foundation_logging_tests)
    set_tests_properties(ludus_foundation_logging_tests PROPERTIES LABELS "unit;foundation;logging")
endif()
