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
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES include/ludus/graphics/rhi/rhi.h
)

target_link_libraries(ludus_graphics_rhi
    PUBLIC
        Ludus::FoundationBase
    PRIVATE
        Ludus::FoundationContainers
        Ludus::FoundationLogging
        volk::volk
)
target_compile_definitions(ludus_graphics_rhi PRIVATE VK_NO_PROTOTYPES)
ludus_apply_project_defaults(ludus_graphics_rhi)

install(TARGETS ludus_graphics_rhi
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
