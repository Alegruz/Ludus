# Faults live only in explicitly driven test children, never runtime libraries.
if(LUDUS_ENABLE_ASAN OR LUDUS_ENABLE_UBSAN OR LUDUS_ENABLE_TSAN)
    get_filename_component(quality_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        get_filename_component(quality_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(quality_compiler_bin "${quality_compiler}" DIRECTORY)
        ludus_get_pinned_llvm_major(quality_llvm_major)
        find_program(LUDUS_QUALITY_SYMBOLIZER NAMES llvm-symbolizer-${quality_llvm_major} llvm-symbolizer
            HINTS "${quality_compiler_bin}" REQUIRED)
    else()
        set(LUDUS_QUALITY_SYMBOLIZER "none")
    endif()
    foreach(kind ASAN UBSAN TSAN)
        if(LUDUS_ENABLE_${kind})
            string(TOLOWER "${kind}" name)
            set(child "ludus_${name}_control_child")
            add_executable(${child} sanitizer_child.cpp)
            target_include_directories(${child} PRIVATE "${quality_source_root}/modules/foundation/base/include")
            target_compile_definitions(${child} PRIVATE "LUDUS_CONTROL_${kind}=1")
            ludus_apply_project_defaults(${child})
            if(kind STREQUAL "TSAN")
                find_package(Threads REQUIRED)
                target_link_libraries(${child} PRIVATE Threads::Threads)
            endif()
            add_test(NAME "ludus_${name}_control"
                COMMAND "${Python3_EXECUTABLE}" "${quality_source_root}/scripts/python/sanitizer_control.py"
                    --kind "${name}" --child "$<TARGET_FILE:${child}>"
                    --symbolizer "${LUDUS_QUALITY_SYMBOLIZER}"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/${name}-evidence")
            set_tests_properties("ludus_${name}_control" PROPERTIES
                LABELS "build-contract;sanitizer-control" TIMEOUT 45)
        endif()
    endforeach()
endif()
