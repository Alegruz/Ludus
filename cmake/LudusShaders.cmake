include_guard(GLOBAL)
# No downloads at configure/build time. Host tools remain separate from target
# compilers, including when cross-compiling to wasm.
function(ludus_compile_shader)
    cmake_parse_arguments(PARSE_ARGV 0 SH "" "TARGET;NAME;SOURCE;VERTEX;FRAGMENT" "INCLUDES;DEFINES;DEPENDS")
    if(SH_UNPARSED_ARGUMENTS OR NOT TARGET "${SH_TARGET}" OR
       NOT SH_NAME MATCHES "^[A-Za-z_][A-Za-z_0-9]*$" OR NOT SH_SOURCE OR NOT SH_VERTEX OR NOT SH_FRAGMENT)
        message(FATAL_ERROR "ludus_compile_shader requires TARGET NAME SOURCE VERTEX FRAGMENT; optional INCLUDES DEFINES DEPENDS")
    endif()
    if(NOT LUDUS_SLANG_COMPILER OR NOT LUDUS_SPIRV_VALIDATOR)
        message(FATAL_ERROR "Set LUDUS_SLANG_COMPILER and LUDUS_SPIRV_VALIDATOR to pinned host tools (shader_toolchain.json)")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    get_filename_component(source "${SH_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/ludus-shaders/${SH_TARGET}/${SH_NAME}")
    set(args)
    foreach(path IN LISTS SH_INCLUDES)
        get_filename_component(path "${path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND args --include "${path}")
    endforeach()
    foreach(define IN LISTS SH_DEFINES)
        list(APPEND args --define "${define}")
    endforeach()
    set(driver "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/compile_shader.py")
    set(lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/shader_toolchain.json")
    if(EXISTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/shader_toolchain.json")
        set(lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/shader_toolchain.json")
    endif()
    add_custom_command(
        OUTPUT "${output}/${SH_NAME}.h"
        BYPRODUCTS "${output}/${SH_NAME}.vertex.spv" "${output}/${SH_NAME}.fragment.spv"
                   "${output}/${SH_NAME}.wgsl" "${output}/vertex.reflection.json"
                   "${output}/fragment.reflection.json" "${output}/wgsl.reflection.json" "${output}/manifest.json"
        COMMAND "${Python3_EXECUTABLE}" "${driver}" --source "${source}" --output "${output}"
                --name "${SH_NAME}" --vertex "${SH_VERTEX}" --fragment "${SH_FRAGMENT}"
                --lock "${lock}" --compiler "${LUDUS_SLANG_COMPILER}" --validator "${LUDUS_SPIRV_VALIDATOR}" ${args}
        DEPENDS "${source}" "${driver}" "${lock}" "${LUDUS_SLANG_COMPILER}" "${LUDUS_SPIRV_VALIDATOR}" ${SH_DEPENDS}
        DEPFILE "${output}/shader.d"
        VERBATIM
    )
    target_sources(${SH_TARGET} PRIVATE "${output}/${SH_NAME}.h")
    target_include_directories(${SH_TARGET} PRIVATE "${output}")
endfunction()
