cmake_minimum_required(VERSION 3.29)
project(LudusCornellBox LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

# Independent consumer: configure never downloads or builds engine sources.
find_package(Ludus CONFIG REQUIRED COMPONENTS FoundationBase FoundationLogging Platform GraphicsRhi)
if(EMSCRIPTEN OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    message(FATAL_ERROR "This tutorial currently supports native Linux/Vulkan and macOS/Metal only")
endif()
add_executable(cornell_box src/main.cpp)
if(APPLE)
    set_target_properties(cornell_box PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.ludus.samples.cornell-box"
        MACOSX_BUNDLE_BUNDLE_NAME "Ludus Cornell Box"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
endif()
target_link_libraries(cornell_box PRIVATE Ludus::FoundationBase Ludus::FoundationLogging Ludus::Platform Ludus::GraphicsRhi)
ludus_apply_app_policy(cornell_box)
target_compile_options(cornell_box PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wundef
    -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Woverloaded-virtual -Wnon-virtual-dtor -Werror)
ludus_compile_shader(TARGET cornell_box NAME cornell SOURCE shaders/cornell.slang
    VERTEX vertexMain FRAGMENT fragmentMain)
option(CORNELL_ENABLE_SANITIZERS "Instrument the sample with ASan and UBSan" OFF)
if(CORNELL_ENABLE_SANITIZERS)
    target_compile_options(cornell_box PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cornell_box PRIVATE -fsanitize=address,undefined)
endif()
if(BUILD_TESTING)
    add_test(NAME cornell_help COMMAND cornell_box --help)
    add_test(NAME cornell_reject_unbounded_headless COMMAND cornell_box --headless)
    set_tests_properties(cornell_reject_unbounded_headless PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cornell_render COMMAND cornell_box --headless --frames=3)
    add_test(NAME cornell_materials COMMAND cornell_box --headless --flat --frames=3)
    set_tests_properties(cornell_help cornell_reject_unbounded_headless cornell_render cornell_materials
        PROPERTIES TIMEOUT 30)
endif()
