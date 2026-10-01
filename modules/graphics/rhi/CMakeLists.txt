if(NOT EMSCRIPTEN)
    find_package(volk CONFIG REQUIRED)
endif()

add_library(ludus_graphics_rhi STATIC)
add_library(Ludus::GraphicsRhi ALIAS ludus_graphics_rhi)

set_target_properties(ludus_graphics_rhi PROPERTIES
    OUTPUT_NAME ludus_graphics_rhi
    EXPORT_NAME GraphicsRhi
)

target_sources(ludus_graphics_rhi
    PRIVATE
        src/rhi.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES include/ludus/graphics/rhi/rhi.h
)

target_link_libraries(ludus_graphics_rhi
    PUBLIC
        Ludus::FoundationBase
        Ludus::Platform
    PRIVATE
        Ludus::FoundationContainers
        Ludus::FoundationLogging
)
if(EMSCRIPTEN)
    target_sources(ludus_graphics_rhi PRIVATE src/rhi_webgpu.cpp)
    target_compile_options(ludus_graphics_rhi PRIVATE "--use-port=emdawnwebgpu:cpp_bindings=false")
    target_link_options(ludus_graphics_rhi INTERFACE "--use-port=emdawnwebgpu:cpp_bindings=false")
else()
    target_sources(ludus_graphics_rhi PRIVATE src/rhi_vulkan.cpp src/volk_impl.cpp)
    target_link_libraries(ludus_graphics_rhi PRIVATE volk::volk_headers)
    target_compile_definitions(ludus_graphics_rhi PRIVATE VK_NO_PROTOTYPES)
    if(LUDUS_PLATFORM_ENABLE_WAYLAND)
        target_compile_definitions(ludus_graphics_rhi PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
endif()
ludus_apply_project_defaults(ludus_graphics_rhi)

# Vulkan structs intentionally use partial designated initializers; omitted output
# fields are zero-initialized. Keep this exception private to the RHI target.
set_target_properties(ludus_graphics_rhi PROPERTIES LUDUS_ALLOW_PARTIAL_INITIALIZERS ON)

install(TARGETS ludus_graphics_rhi
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_graphics_rhi_tests tests/rhi_tests.cpp)
    target_include_directories(ludus_graphics_rhi_tests PRIVATE src)
    target_link_libraries(ludus_graphics_rhi_tests PRIVATE
        Ludus::GraphicsRhi Ludus::FoundationLogging volk::volk_headers Catch2::Catch2WithMain)
    if(LUDUS_PLATFORM_ENABLE_WAYLAND)
        target_compile_definitions(ludus_graphics_rhi_tests PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
    ludus_apply_project_defaults(ludus_graphics_rhi_tests)
    ludus_enable_test_exceptions(ludus_graphics_rhi_tests)
    add_test(NAME ludus_graphics_rhi_tests COMMAND ludus_graphics_rhi_tests)
    set_tests_properties(ludus_graphics_rhi_tests PROPERTIES TIMEOUT 60)
endif()

if(LUDUS_BUILD_TESTS)
    add_executable(ludus_rhi_lifecycle_tests tests/lifecycle_tests.cpp src/rhi.cpp)
    target_include_directories(ludus_rhi_lifecycle_tests PRIVATE include src)
    target_link_libraries(ludus_rhi_lifecycle_tests PRIVATE Ludus::Platform Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_rhi_lifecycle_tests)
    ludus_enable_test_exceptions(ludus_rhi_lifecycle_tests)
    add_test(NAME ludus_rhi_lifecycle_tests COMMAND ludus_rhi_lifecycle_tests)
endif()
