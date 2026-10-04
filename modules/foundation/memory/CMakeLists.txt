add_library(ludus_foundation_memory STATIC)
add_library(Ludus::FoundationMemory ALIAS ludus_foundation_memory)
set_target_properties(ludus_foundation_memory PROPERTIES OUTPUT_NAME ludus_foundation_memory EXPORT_NAME FoundationMemory)
target_sources(ludus_foundation_memory
    PRIVATE
        src/allocation_domain.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
        include/ludus/foundation/memory/allocation_domain.hpp
)
target_link_libraries(ludus_foundation_memory PUBLIC Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_foundation_memory)
install(TARGETS ludus_foundation_memory EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
