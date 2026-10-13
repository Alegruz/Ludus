add_library(ludus_graphics_renderer STATIC src/renderer.cpp src/viewport.cpp src/views.cpp src/overlays.cpp src/lighting.cpp src/reference_bsdf.cpp src/reference_scene.cpp src/reference_path.cpp)
add_library(Ludus::GraphicsRenderer ALIAS ludus_graphics_renderer)
set_target_properties(ludus_graphics_renderer PROPERTIES EXPORT_NAME GraphicsRenderer)
target_sources(ludus_graphics_renderer PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
    include/ludus/graphics/renderer/renderer.hpp
    include/ludus/graphics/renderer/views.hpp
    include/ludus/graphics/renderer/overlays.hpp
    include/ludus/graphics/renderer/materials.hpp
    include/ludus/graphics/renderer/lighting.hpp
    include/ludus/graphics/renderer/reference.hpp
    include/ludus/graphics/renderer/gpu_preview.hpp
    include/ludus/graphics/renderer/viewport.hpp)
target_link_libraries(ludus_graphics_renderer PUBLIC Ludus::FoundationBase Ludus::FoundationMath Ludus::GraphicsRhi
    PRIVATE Ludus::FoundationContainers Ludus::FoundationHash)
target_compile_options(ludus_graphics_renderer PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
ludus_apply_project_defaults(ludus_graphics_renderer)
install(TARGETS ludus_graphics_renderer EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
# Source is cooked by the existing installed shader helper; no runtime compiler dependency.
install(FILES shaders/recipes.json shaders/renderer_gpu_preview_layout.h shaders/renderer_flat.slang shaders/renderer_composite.slang shaders/renderer_overlay.slang shaders/renderer_material.slang shaders/renderer_lit.slang shaders/renderer_clustered.slang shaders/renderer_shadow.slang shaders/renderer_output.slang shaders/renderer_gpu_preview.slang shaders/renderer_gpu_preview_image.slang shaders/renderer_surface.slang shaders/renderer_clustered_surface.slang shaders/renderer_surface_functions.slang DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Ludus/shaders")
if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    # Exercise the production renderer/RHI registry with the existing deterministic backend.
    add_executable(ludus_renderer_tests tests/renderer_tests.cpp tests/reference_backend.cpp src/renderer.cpp src/viewport.cpp src/views.cpp src/overlays.cpp src/lighting.cpp src/reference_bsdf.cpp src/reference_scene.cpp src/reference_path.cpp tests/views_tests.cpp
        ../rhi/src/rhi.cpp ../rhi/src/device.cpp ../rhi/src/profile.cpp ../rhi/src/raster.cpp ../rhi/tests/reference_raster.cpp ../rhi/src/ray_unsupported.cpp)
    target_include_directories(ludus_renderer_tests PRIVATE include ../rhi/include ../rhi/src ../rhi/tests "${PROJECT_SOURCE_DIR}/tests/sdk_consumer")
    target_link_libraries(ludus_renderer_tests PRIVATE Ludus::Platform Ludus::FoundationContainers Ludus::FoundationMath Ludus::Resources Ludus::FoundationHash Catch2::Catch2WithMain)
    target_compile_options(ludus_renderer_tests PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
    ludus_apply_project_defaults(ludus_renderer_tests)
    ludus_enable_test_exceptions(ludus_renderer_tests)
    add_test(NAME ludus_renderer_tests COMMAND ludus_renderer_tests)
    set_tests_properties(ludus_renderer_tests PROPERTIES LABELS "unit;renderer;build-contract")
endif()
if(TARGET ludus_rhi_render_tests)
    add_executable(ludus_renderer_render_tests tests/render_tests.cpp tests/gpu_preview_tests.cpp)
    target_include_directories(ludus_renderer_render_tests PRIVATE ../rhi/src "${PROJECT_SOURCE_DIR}/tests/sdk_consumer")
    target_link_libraries(ludus_renderer_render_tests PRIVATE Ludus::GraphicsText Ludus::FoundationTime Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_renderer_render_tests)
    ludus_enable_test_exceptions(ludus_renderer_render_tests)
    if(APPLE)
        target_compile_definitions(ludus_renderer_render_tests PRIVATE LUDUS_TEST_METAL=1)
    endif()
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_gpu_preview)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_gpu_preview_image)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_flat)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_composite)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_overlay)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_material)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_clustered)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_clustered_shadow)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_lit)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_lit_shadow)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_shadow)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS renderer_output)
    ludus_compile_renderer_shaders(TARGET ludus_renderer_render_tests SHADERS
        renderer_surface renderer_surface_shadow renderer_clustered_surface renderer_clustered_surface_shadow)
    ludus_cook_texture(TARGET ludus_renderer_render_tests NAME l4_bake SOURCE "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/l4-bake.texture.json")
    ludus_cook_texture(TARGET ludus_renderer_render_tests NAME l3_color
        SOURCE "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/l3-color.texture.json")
    add_test(NAME ludus_renderer_pixels COMMAND ludus_renderer_render_tests)
    set_tests_properties(ludus_renderer_pixels PROPERTIES LABELS "renderer;gpu" TIMEOUT 60)
    if(APPLE)
        set_property(TEST ludus_renderer_pixels APPEND PROPERTY LABELS macos)
    endif()
endif()

if(TARGET ludus_renderer_render_tests AND LUDUS_SPIRV_CROSS)
    target_compile_definitions(ludus_renderer_render_tests PRIVATE LUDUS_TEST_RAY=1)
    ludus_compile_ray_queries(TARGET ludus_renderer_render_tests NAME ray_query)
endif()
