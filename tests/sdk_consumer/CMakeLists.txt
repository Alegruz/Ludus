cmake_minimum_required(VERSION 3.29)

project(LudusSdkConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Ludus CONFIG REQUIRED)

if(NOT EMSCRIPTEN)
    add_executable(ludus_sdk_consumer main.cpp policy.cpp world.cpp strings.cpp time.cpp fluid.cpp)
    target_link_libraries(ludus_sdk_consumer PRIVATE Ludus::FoundationStrings Ludus::FoundationBase Ludus::FoundationMath Ludus::FoundationTime Ludus::GraphicsRhi Ludus::Input Ludus::GameplayWorld Ludus::PhysicsFluid)
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
