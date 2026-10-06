include_guard(GLOBAL)
# No downloads at configure/build time. Host tools remain separate from target
# compilers, including when cross-compiling to wasm.
function(ludus_compile_shader)
    cmake_parse_arguments(PARSE_ARGV 0 SH "" "TARGET;NAME;SOURCE;VERTEX;FRAGMENT" "INCLUDES;DEFINES;DEPENDS")
    if(SH_UNPARSED_ARGUMENTS OR NOT TARGET "${SH_TARGET}" OR
       NOT SH_NAME MATCHES "^[A-Za-z_][A-Za-z_0-9]*$" OR NOT SH_SOURCE OR NOT SH_VERTEX OR NOT SH_FRAGMENT)
        message(FATAL_ERROR "ludus_compile_shader requires TARGET NAME SOURCE VERTEX FRAGMENT; optional INCLUDES DEFINES DEPENDS")
    endif()
    if(NOT LUDUS_SLANG_COMPILER)
        message(FATAL_ERROR "Set LUDUS_SLANG_COMPILER to the pinned host tool; run scripts/shader-probe bootstrap")
    endif()
    set(backend_args)
    set(backend_depends)
    set(backend_byproducts)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND backend_args --metal)
        list(APPEND backend_byproducts "${SH_NAME}.vertex.metal" "${SH_NAME}.fragment.metal")
    else()
        if(NOT LUDUS_SPIRV_VALIDATOR)
            message(FATAL_ERROR "Set LUDUS_SPIRV_VALIDATOR to the pinned host tool (shader_toolchain.json)")
        endif()
        list(APPEND backend_args --validator "${LUDUS_SPIRV_VALIDATOR}")
        list(APPEND backend_depends "${LUDUS_SPIRV_VALIDATOR}")
        list(APPEND backend_byproducts "${SH_NAME}.vertex.spv" "${SH_NAME}.fragment.spv" "${SH_NAME}.wgsl" "wgsl.reflection.json")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    get_filename_component(source "${SH_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/ludus-shaders/${SH_TARGET}/${SH_NAME}")
    list(TRANSFORM backend_byproducts PREPEND "${output}/")
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
    # Optional GLSL ES 3.00 (WebGL 2) backend artifact. Enabled only when the
    # pinned SPIR-V -> GLSL ES translator is provided; SPIR-V/WGSL builds are
    # unchanged otherwise. The browser build carries both WGSL and GLSL ES.
    if(EMSCRIPTEN AND NOT LUDUS_SPIRV_CROSS)
        message(FATAL_ERROR "Browser Auto shaders require LUDUS_SPIRV_CROSS; run scripts/bootstrap-spirv-cross")
    endif()
    set(cross_args)
    set(cross_depends)
    set(cross_byproducts)
    if(LUDUS_SPIRV_CROSS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(cross_lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/spirv_cross_toolchain.json")
        if(EXISTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/spirv_cross_toolchain.json")
            set(cross_lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/spirv_cross_toolchain.json")
        endif()
        list(APPEND cross_args --spirv-cross "${LUDUS_SPIRV_CROSS}" --spirv-cross-lock "${cross_lock}")
        list(APPEND cross_depends "${LUDUS_SPIRV_CROSS}" "${LUDUS_SPIRV_CROSS}.build.json" "${cross_lock}")
        list(APPEND cross_byproducts "${output}/${SH_NAME}.vertex.essl" "${output}/${SH_NAME}.fragment.essl"
            "${output}/${SH_NAME}.vertex.glsl-es.spv" "${output}/${SH_NAME}.fragment.glsl-es.spv"
            "${output}/vertex.glsl-es.reflection.json" "${output}/fragment.glsl-es.reflection.json")
    endif()
    add_custom_command(
        OUTPUT "${output}/${SH_NAME}.h"
        BYPRODUCTS ${backend_byproducts} "${output}/vertex.reflection.json"
                   "${output}/fragment.reflection.json" "${output}/manifest.json"
                   ${cross_byproducts}
        COMMAND "${Python3_EXECUTABLE}" "${driver}" --source "${source}" --output "${output}"
                --name "${SH_NAME}" --vertex "${SH_VERTEX}" --fragment "${SH_FRAGMENT}"
                --lock "${lock}" --compiler "${LUDUS_SLANG_COMPILER}" ${backend_args} ${cross_args} ${args}
        DEPENDS "${source}" "${driver}" "${lock}" "${LUDUS_SLANG_COMPILER}" ${backend_depends} ${cross_depends} ${SH_DEPENDS}
        DEPFILE "${output}/shader.d"
        VERBATIM
    )
    target_sources(${SH_TARGET} PRIVATE "${output}/${SH_NAME}.h")
    target_include_directories(${SH_TARGET} PRIVATE "${output}")
endfunction()
