function(ludus_configure_project_options target_name)
    add_library(${target_name} INTERFACE)

    target_compile_features(${target_name} INTERFACE cxx_std_23)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(${target_name} INTERFACE -fuse-ld=lld)
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
