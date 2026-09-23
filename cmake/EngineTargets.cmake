function(ludus_apply_project_defaults target_name)
    target_link_libraries(
        ${target_name}
        PRIVATE
            $<BUILD_INTERFACE:ludus_project_options>
            $<BUILD_INTERFACE:ludus_project_warnings>
    )
endfunction()

# Re-enable C++ exceptions for a single target. Engine code is compiled with
# -fno-exceptions (see EngineOptions.cmake); test executables need exceptions
# because Catch2 reports assertion failures by throwing. Apply this ONLY to test
# targets, never to engine libraries or applications.
#
# Setting LUDUS_TEST_EXCEPTIONS suppresses the interface `-fno-exceptions`
# (see EngineOptions.cmake) so this target compiles with `-fexceptions` only,
# with no conflicting `-fno-exceptions` on the same command line. This is
# order-independent, unlike appending `-fexceptions` after an inherited
# `-fno-exceptions` (which CMake ordered so that -fno-exceptions won, silently
# disabling exceptions in tests). Verified with the compile database.
function(ludus_enable_test_exceptions target_name)
    set_target_properties(${target_name} PROPERTIES LUDUS_TEST_EXCEPTIONS ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target_name} PRIVATE -fexceptions)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /EHsc)
    endif()
endfunction()
