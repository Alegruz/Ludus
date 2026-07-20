function(ludus_configure_project_sanitizers target_name)
    if(LUDUS_ENABLE_ASAN AND LUDUS_ENABLE_TSAN)
        message(FATAL_ERROR "LUDUS_ENABLE_ASAN and LUDUS_ENABLE_TSAN cannot be enabled together")
    endif()

    if(LUDUS_ENABLE_TSAN AND LUDUS_ENABLE_COVERAGE)
        message(FATAL_ERROR "LUDUS_ENABLE_TSAN and LUDUS_ENABLE_COVERAGE cannot be enabled together")
    endif()

    set(enabled_sanitizers "")

    if(LUDUS_ENABLE_ASAN)
        list(APPEND enabled_sanitizers "address")
    endif()

    if(LUDUS_ENABLE_UBSAN)
        list(APPEND enabled_sanitizers "undefined")
    endif()

    if(LUDUS_ENABLE_TSAN)
        list(APPEND enabled_sanitizers "thread")
    endif()

    if(enabled_sanitizers)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR "Sanitizer presets require Clang or GCC")
        endif()

        list(JOIN enabled_sanitizers "," sanitizer_flags)
        target_compile_options(${target_name} INTERFACE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
        target_link_options(${target_name} INTERFACE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
    endif()
endfunction()
