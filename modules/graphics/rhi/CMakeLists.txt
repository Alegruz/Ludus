find_package(volk CONFIG REQUIRED)

add_library(ludus_graphics_rhi STATIC)
add_library(Ludus::GraphicsRhi ALIAS ludus_graphics_rhi)

set_target_properties(ludus_graphics_rhi PROPERTIES
    OUTPUT_NAME ludus_graphics_rhi
    EXPORT_NAME GraphicsRhi
)

target_sources(ludus_graphics_rhi
    PRIVATE
        src/rhi.cpp
        src/volk_impl.cpp
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
        volk::volk_headers
)
target_compile_definitions(ludus_graphics_rhi PRIVATE VK_NO_PROTOTYPES)
if(LUDUS_PLATFORM_ENABLE_WAYLAND)
    target_compile_definitions(ludus_graphics_rhi PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
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
    target_link_libraries(ludus_graphics_rhi_tests PRIVATE
        Ludus::GraphicsRhi volk::volk_headers Catch2::Catch2WithMain)
    if(LUDUS_PLATFORM_ENABLE_WAYLAND)
        target_compile_definitions(ludus_graphics_rhi_tests PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
    ludus_apply_project_defaults(ludus_graphics_rhi_tests)
    ludus_enable_test_exceptions(ludus_graphics_rhi_tests)
    add_test(NAME ludus_graphics_rhi_tests COMMAND ludus_graphics_rhi_tests)
    set_tests_properties(ludus_graphics_rhi_tests PROPERTIES TIMEOUT 60)
endif()
