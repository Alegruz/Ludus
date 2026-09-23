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
# targets, never to engine libraries or applications. The flag is appended after
# the inherited -fno-exceptions, and the last -f option on the command line wins.
function(ludus_enable_test_exceptions target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target_name} PRIVATE -fexceptions)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /EHsc)
    endif()
endfunction()
