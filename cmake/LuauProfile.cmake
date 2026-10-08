# Reviewed interpreter profile shared by S0-S6. Behavior installs a private
# static link closure; six-platform/device qualification remains in progress.
include_guard(GLOBAL)
function(ludus_prepare_luau)
    if(TARGET Luau.VM)
        return()
    endif()
    ludus_require_python("Luau scripting")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/luau-probe" verify-sources
        RESULT_VARIABLE verified OUTPUT_VARIABLE verification_output ERROR_VARIABLE verification_error TIMEOUT 30)
    if(NOT verified EQUAL 0)
        ludus_dependency_error("Luau scripting" "the reviewed source profile"
            "Source verification failed: ${verification_output}${verification_error}"
            "From the Ludus source repository root: ./scripts/luau-probe bootstrap")
    endif()
    set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
    set(LUAU_EXTERN_C ON CACHE BOOL "" FORCE)
    set(LUAU_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(LUAU_WERROR ON CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/out/luau-probe/source"
        "${PROJECT_BINARY_DIR}/luau-profile" EXCLUDE_FROM_ALL SYSTEM)
    add_custom_target(ludus_luau_source_verify
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/luau-probe" verify-sources)
    add_dependencies(Luau.VM ludus_luau_source_verify)
    foreach(target Luau.VM Luau.Common)
        target_link_libraries(${target} PRIVATE ludus_project_options)
        set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    endforeach()
    if(EMSCRIPTEN)
        target_compile_options(Luau.VM PRIVATE -sSUPPORT_LONGJMP=wasm)
    endif()
endfunction()
