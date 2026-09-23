function(ludus_configure_project_options target_name)
    add_library(${target_name} INTERFACE)

    target_compile_features(${target_name} INTERFACE cxx_std_23)

    target_compile_definitions(${target_name} INTERFACE
        LUDUS_BUILD_${LUDUS_BUILD_FLAVOR_DEFINE}=1
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${target_name} INTERFACE -fuse-ld=lld)
        # Map diagnostic literals without breaking DWARF's source/comp-dir pair.
        target_compile_options(${target_name} INTERFACE "-fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=")
    endif()

    # Ludus does not use C++ exceptions (see AGENTS.md and the steering rule
    # "no-exceptions"). Compile them out entirely so error handling stays
    # explicit (status/optional/return values) and so any accidental throw /
    # try / catch fails to compile rather than slipping through review. Test
    # executables re-enable exceptions via ludus_enable_test_exceptions()
    # because Catch2 reports failures by throwing.
    #
    # The exception flag is gated on the per-target LUDUS_TEST_EXCEPTIONS
    # property rather than emitted unconditionally. Emitting both
    # `-fexceptions` (from the test helper) and `-fno-exceptions` (from this
    # interface) on the same command line let the *last* flag win, and CMake
    # orders linked-interface options after a target's own options, so
    # `-fno-exceptions` silently won even in test targets (the helper's
    # documented "last -f wins" intent did not hold). Gating removes the flag
    # entirely for opted-in test targets, so there is no conflicting pair and
    # the result is order-independent. See EngineTargets.cmake.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target_name} INTERFACE
            $<$<NOT:$<BOOL:$<TARGET_PROPERTY:LUDUS_TEST_EXCEPTIONS>>>:-fno-exceptions>)
    elseif(MSVC)
        target_compile_options(${target_name} INTERFACE
            $<$<NOT:$<BOOL:$<TARGET_PROPERTY:LUDUS_TEST_EXCEPTIONS>>>:/EHs-c->)
    endif()

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${target_name} INTERFACE $<$<CXX_COMPILER_ID:Clang,GNU>:-UNDEBUG>)
    endif()

    if(LUDUS_ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} INTERFACE -O0 -g --coverage)
            target_link_options(${target_name} INTERFACE --coverage)
        else()
            message(FATAL_ERROR "LUDUS_ENABLE_COVERAGE is only supported for Clang and GCC in Milestone 0")
        endif()
    endif()

    # Build-time profiling. -ftime-trace makes Clang emit a per-translation-unit
    # JSON flame graph (next to each .o) describing where compile time went:
    # headers parsed, template instantiations, constexpr evaluation. The traces
    # are aggregated by scripts/profile-build (see ClangBuildAnalyzer). This is a
    # diagnostic build mode only; leave it OFF for normal builds.
    if(LUDUS_ENABLE_TIME_TRACE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
            target_compile_options(${target_name} INTERFACE -ftime-trace)
        else()
            message(FATAL_ERROR "LUDUS_ENABLE_TIME_TRACE requires Clang")
        endif()
    endif()
endfunction()
