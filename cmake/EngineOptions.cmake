function(ludus_configure_project_options target_name)
    add_library(${target_name} INTERFACE)

    target_compile_features(${target_name} INTERFACE cxx_std_23)

    target_compile_definitions(${target_name} INTERFACE
        $<$<CONFIG:Debug>:LUDUS_BUILD_DEBUG=1>
        $<$<CONFIG:RelWithDebInfo>:LUDUS_BUILD_DEVELOPMENT=1>
        $<$<CONFIG:Release>:LUDUS_BUILD_RELEASE=1>
        $<$<CONFIG:MinSizeRel>:LUDUS_BUILD_RELEASE=1>
        $<$<CONFIG:Profile>:LUDUS_BUILD_PROFILE=1>
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${target_name} INTERFACE -fuse-ld=lld)
    endif()

    # Ludus does not use C++ exceptions (see AGENTS.md and the steering rule
    # "no-exceptions"). Compile them out entirely so error handling stays
    # explicit (status/optional/return values) and so any accidental throw /
    # try / catch fails to compile rather than slipping through review. Test
    # executables re-enable exceptions via ludus_enable_test_exceptions()
    # because Catch2 reports failures by throwing.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target_name} INTERFACE -fno-exceptions)
    elseif(MSVC)
        target_compile_options(${target_name} INTERFACE /EHs-c-)
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
endfunction()
