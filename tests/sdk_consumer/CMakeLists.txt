cmake_minimum_required(VERSION 3.29)

project(LudusSdkConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Ludus CONFIG REQUIRED COMPONENTS FoundationReflection FoundationSerialization GamePropertyBinding Ui FoundationParsing FoundationParsingJson FoundationConfig FoundationConfigJson RuntimeConfiguration Localization OPTIONAL_COMPONENTS MissingUiTestComponent)
if(Ludus_MissingUiTestComponent_FOUND)
    message(FATAL_ERROR "Unknown optional SDK component was reported as available")
endif()
# An unknown required component must fail for that precise reason, rather than
# being silently accepted or failing because of a missing compiler/dependency.
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_SOURCE_DIR}/components" -B "${CMAKE_CURRENT_BINARY_DIR}/missing-component"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DCMAKE_FIND_ROOT_PATH=${CMAKE_FIND_ROOT_PATH}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DLudus_DIR=${Ludus_DIR}"
    RESULT_VARIABLE _missing_result OUTPUT_VARIABLE _missing_output ERROR_VARIABLE _missing_error)
if(_missing_result EQUAL 0 OR NOT "${_missing_output}${_missing_error}" MATCHES
        "Requested Ludus SDK component 'MissingUiTestComponent' is unavailable")
    message(FATAL_ERROR "Required missing-component rejection failed: ${_missing_output}${_missing_error}")
endif()

ludus_generate_reflection(TARGET consumer_reflection SCHEMA reflection_record.schema.json)

if(NOT EMSCRIPTEN)
    # A relocated SDK must also support codecs inside a private gameplay module.
    add_library(ludus_sdk_reflection_module MODULE reflection_module.cpp)
    target_link_libraries(ludus_sdk_reflection_module PRIVATE consumer_reflection)
    ludus_apply_app_policy(ludus_sdk_reflection_module)
    set_target_properties(ludus_sdk_reflection_module PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ludus_sdk_reflection_module PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
    add_executable(ludus_sdk_consumer main.cpp reflection.cpp network.cpp policy.cpp world.cpp strings.cpp time.cpp fluid.cpp filesystem.cpp pack.cpp async.cpp persistence.cpp ui.cpp containers.cpp threading.cpp parsing.cpp localization.cpp configuration.cpp curves.cpp content.cpp audio.cpp)
    target_link_libraries(ludus_sdk_consumer PRIVATE consumer_reflection Ludus::AudioContent Ludus::Content Ludus::FoundationConfigJson Ludus::RuntimeConfiguration Ludus::NetworkCore Ludus::FoundationParsing Ludus::FoundationParsingJson Ludus::Localization Ludus::FoundationContainers Ludus::FoundationFilesystem Ludus::FoundationStrings Ludus::FoundationBase Ludus::FoundationMath Ludus::FoundationThreading Ludus::FoundationTime Ludus::GraphicsRhi Ludus::Input Ludus::GameplayWorld Ludus::PhysicsFluid Ludus::Ui)
    target_compile_definitions(ludus_sdk_consumer PRIVATE
        EXPECTED_FLAVOR=${Ludus_BUILD_FLAVOR_ID}
        EXPECTED_ASSERTS=${Ludus_ENABLE_ASSERTS}
        EXPECTED_CHECK_BREAK=${Ludus_BREAK_ON_CHECK})
endif()
option(LUDUS_SDK_RENDER_TESTS "Build generic public rendering consumer" OFF)
if(LUDUS_SDK_RENDER_TESTS)
    add_executable(ludus_sdk_render_consumer render.cpp)
    target_link_libraries(ludus_sdk_render_consumer PRIVATE Ludus::GraphicsRhi)
    target_compile_options(ludus_sdk_render_consumer PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
    ludus_compile_shader(TARGET ludus_sdk_render_consumer NAME diagnostic SOURCE shaders/diagnostic.slang
        VERTEX vertexMain FRAGMENT fragmentMain INCLUDES shaders)
    if(EMSCRIPTEN)
        target_link_options(ludus_sdk_render_consumer PRIVATE -fno-exceptions -g2 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
        set_target_properties(ludus_sdk_render_consumer PROPERTIES OUTPUT_NAME render SUFFIX ".js")
        configure_file(render.html index.html COPYONLY)
    endif()
endif()
