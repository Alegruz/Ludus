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
        FILES include/ludus/graphics/rhi/rhi.h include/ludus/graphics/rhi/render.h
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
    # Browser Auto build: both web sub-backends plus the dispatcher are linked
    # into one artifact, each sub-backend in its own leaf namespace, and the
    # dispatcher provides backend:: with the WebGPU->WebGL 2 fallback policy.
    target_sources(ludus_graphics_rhi PRIVATE src/rhi_web.cpp src/rhi_webgpu.cpp src/rhi_webgl.cpp)
    set_source_files_properties(src/rhi_webgpu.cpp PROPERTIES
        COMPILE_DEFINITIONS "LUDUS_RHI_WEBGPU_NAMESPACE=webgpu")
    set_source_files_properties(src/rhi_webgl.cpp PROPERTIES
        COMPILE_DEFINITIONS "LUDUS_RHI_WEBGL_NAMESPACE=webgl")
    target_compile_options(ludus_graphics_rhi PRIVATE "--use-port=emdawnwebgpu:cpp_bindings=false")
    # Propagate both browser backends' link requirements to installed consumers:
    # the emdawnwebgpu port (WebGPU) and the WebGL 2 / full GLES3 settings. An
    # Auto build therefore carries both browser paths.
    target_link_options(ludus_graphics_rhi INTERFACE
        "--use-port=emdawnwebgpu:cpp_bindings=false"
        "-sMAX_WEBGL_VERSION=2"
        "-sMIN_WEBGL_VERSION=2"
        "-sFULL_ES3=1")
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

if(LUDUS_BUILD_TESTS AND LUDUS_SLANG_COMPILER AND LUDUS_SPIRV_VALIDATOR)
    target_compile_definitions(ludus_graphics_rhi PRIVATE LUDUS_RHI_TEST_READBACK=1)
    add_executable(ludus_rhi_render_tests tests/render_tests.cpp)
    target_include_directories(ludus_rhi_render_tests PRIVATE src)
    target_link_libraries(ludus_rhi_render_tests PRIVATE Ludus::GraphicsRhi Ludus::FoundationLogging Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_rhi_render_tests)
    ludus_enable_test_exceptions(ludus_rhi_render_tests)
    ludus_compile_shader(TARGET ludus_rhi_render_tests NAME diagnostic
        SOURCE "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/shaders/diagnostic.slang"
        VERTEX vertexMain FRAGMENT fragmentMain)
    add_test(NAME ludus_rhi_render_pixels COMMAND ludus_rhi_render_tests)
    set_tests_properties(ludus_rhi_render_pixels PROPERTIES LABELS "rhi;gpu" TIMEOUT 60)
endif()
