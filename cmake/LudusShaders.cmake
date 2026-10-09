include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/LudusDependencies.cmake")
# No downloads at configure/build time. Host tools remain separate from target
# compilers, including when cross-compiling to wasm.
function(ludus_compile_shader)
    cmake_parse_arguments(PARSE_ARGV 0 SH "RASTER" "TARGET;NAME;SOURCE;VERTEX;FRAGMENT;COMPUTE" "INCLUDES;DEFINES;DEPENDS")
    if(SH_UNPARSED_ARGUMENTS OR NOT TARGET "${SH_TARGET}" OR
       NOT SH_NAME MATCHES "^[A-Za-z_][A-Za-z_0-9]*$" OR NOT SH_SOURCE OR (NOT SH_COMPUTE AND (NOT SH_VERTEX OR NOT SH_FRAGMENT)) OR (SH_COMPUTE AND (SH_VERTEX OR SH_FRAGMENT OR SH_RASTER)))
        message(FATAL_ERROR "ludus_compile_shader requires TARGET NAME SOURCE and either VERTEX/FRAGMENT or COMPUTE; optional RASTER INCLUDES DEFINES DEPENDS")
    endif()
    ludus_require_tool(LUDUS_SLANG_COMPILER "Shader compilation"
        "From the Ludus source repository root: ./scripts/shader-probe bootstrap" -version)
    set(backend_args)
    if(SH_RASTER)
        list(APPEND backend_args --raster)
    endif()
    set(stage_args --vertex "${SH_VERTEX}" --fragment "${SH_FRAGMENT}")
    set(reflection_byproducts "vertex.reflection.json" "fragment.reflection.json")
    if(SH_COMPUTE)
        set(stage_args --compute "${SH_COMPUTE}")
        set(reflection_byproducts)
    endif()
    set(backend_depends)
    set(backend_byproducts)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND backend_args --metal)
        list(APPEND backend_byproducts "${SH_NAME}.vertex.metal" "${SH_NAME}.fragment.metal")
    else()
        ludus_require_tool(LUDUS_SPIRV_VALIDATOR "SPIR-V shader validation"
            "On the Linux host, from the Ludus source repository root: ./scripts/shader-probe bootstrap" --version)
        list(APPEND backend_args --validator "${LUDUS_SPIRV_VALIDATOR}")
        list(APPEND backend_depends "${LUDUS_SPIRV_VALIDATOR}")
        list(APPEND backend_byproducts "${SH_NAME}.vertex.spv" "${SH_NAME}.fragment.spv" "${SH_NAME}.wgsl" "wgsl.reflection.json")
    endif()
    if(SH_RASTER AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(REMOVE_ITEM backend_byproducts "${SH_NAME}.wgsl" "wgsl.reflection.json")
        list(APPEND backend_byproducts "${SH_NAME}.vertex.wgsl" "${SH_NAME}.fragment.wgsl"
             "vertex.wgsl.reflection.json" "fragment.wgsl.reflection.json")
    endif()
    if(SH_COMPUTE)
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(backend_byproducts "${SH_NAME}.compute.metal" "compute.reflection.json")
        else()
            set(backend_byproducts "${SH_NAME}.compute.spv" "${SH_NAME}.compute.wgsl" "compute.reflection.json" "compute.wgsl.reflection.json")
        endif()
    endif()
    ludus_require_python("Shader compilation")
    get_filename_component(source "${SH_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    ludus_require_file("${source}" "Shader compilation" "Restore the shader source or correct SOURCE in ludus_compile_shader.")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/ludus-shaders/${SH_TARGET}/${SH_NAME}")
    list(TRANSFORM backend_byproducts PREPEND "${output}/")
    list(TRANSFORM reflection_byproducts PREPEND "${output}/")
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
    if(NOT SH_COMPUTE AND (EMSCRIPTEN OR (LUDUS_SPIRV_CROSS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")))
        ludus_require_tool(LUDUS_SPIRV_CROSS "GLSL ES shader translation"
            "From the Ludus source repository root: ./scripts/bootstrap-spirv-cross")
        ludus_require_file("${LUDUS_SPIRV_CROSS}.build.json" "GLSL ES shader translation"
            "Restore the matching tool provenance: ./scripts/bootstrap-spirv-cross in the Ludus source repository.")
    endif()
    set(cross_args)
    set(cross_depends)
    set(cross_byproducts)
    if(NOT SH_COMPUTE AND LUDUS_SPIRV_CROSS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(cross_lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/spirv_cross_toolchain.json")
        if(EXISTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/spirv_cross_toolchain.json")
            set(cross_lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../config/spirv_cross_toolchain.json")
        endif()
        list(APPEND cross_args --spirv-cross "${LUDUS_SPIRV_CROSS}" --spirv-cross-lock "${cross_lock}")
        list(APPEND cross_depends "${LUDUS_SPIRV_CROSS}" "${LUDUS_SPIRV_CROSS}.build.json" "${cross_lock}")
        list(APPEND cross_byproducts "${output}/${SH_NAME}.vertex.essl" "${output}/${SH_NAME}.fragment.essl")
        if(NOT SH_RASTER)
            list(APPEND cross_byproducts
                "${output}/${SH_NAME}.vertex.glsl-es.spv" "${output}/${SH_NAME}.fragment.glsl-es.spv"
                "${output}/vertex.glsl-es.reflection.json" "${output}/fragment.glsl-es.reflection.json")
        endif()
    endif()
    add_custom_command(
        OUTPUT "${output}/${SH_NAME}.h"
        BYPRODUCTS ${backend_byproducts} ${reflection_byproducts} "${output}/manifest.json"
                   ${cross_byproducts}
        COMMAND "${Python3_EXECUTABLE}" "${driver}" --source "${source}" --output "${output}"
                --name "${SH_NAME}" ${stage_args}
                --lock "${lock}" --compiler "${LUDUS_SLANG_COMPILER}" ${backend_args} ${cross_args} ${args}
        DEPENDS "${source}" "${driver}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/compile_raster.py" "${lock}" "${LUDUS_SLANG_COMPILER}" ${backend_depends} ${cross_depends} ${SH_DEPENDS}
        DEPFILE "${output}/shader.d"
        VERBATIM
    )
    target_sources(${SH_TARGET} PRIVATE "${output}/${SH_NAME}.h")
    target_include_directories(${SH_TARGET} PRIVATE "${output}")
endfunction()

# Cook a bounded raw-texel JSON source into immutable portable upload bytes/mips.
# This performs no downloads and requires only the existing host Python tool.
function(ludus_cook_texture)
    cmake_parse_arguments(PARSE_ARGV 0 TEX "" "TARGET;NAME;SOURCE" "")
    if(TEX_UNPARSED_ARGUMENTS OR NOT TARGET "${TEX_TARGET}" OR
       NOT TEX_NAME MATCHES "^[A-Za-z_][A-Za-z_0-9]*$" OR NOT TEX_SOURCE)
        message(FATAL_ERROR "ludus_cook_texture requires TARGET NAME SOURCE")
    endif()
    get_filename_component(source "${TEX_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    ludus_require_file("${source}" "Texture cooking" "Restore the authored raw-texel source.")
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/ludus-textures/${TEX_TARGET}/${TEX_NAME}")
    set(driver "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders/cook_texture.py")
    add_custom_command(OUTPUT "${output}/${TEX_NAME}.h"
        COMMAND "${Python3_EXECUTABLE}" "${driver}" --source "${source}" --name "${TEX_NAME}" --output "${output}"
        DEPENDS "${source}" "${driver}" BYPRODUCTS "${output}/manifest.json" VERBATIM)
    target_sources("${TEX_TARGET}" PRIVATE "${output}/${TEX_NAME}.h")
    target_include_directories("${TEX_TARGET}" PRIVATE "${output}")
endfunction()
