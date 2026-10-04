add_library(ludus_foundation_hash STATIC)
add_library(Ludus::FoundationHash ALIAS ludus_foundation_hash)
set_target_properties(ludus_foundation_hash PROPERTIES OUTPUT_NAME ludus_foundation_hash EXPORT_NAME FoundationHash)
target_sources(ludus_foundation_hash
    PRIVATE
        src/hash.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include FILES
        include/ludus/foundation/hash/hash.hpp
)
target_link_libraries(ludus_foundation_hash PUBLIC Ludus::FoundationBase)
target_include_directories(ludus_foundation_hash SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/xxhash")
ludus_apply_project_defaults(ludus_foundation_hash)
install(TARGETS ludus_foundation_hash EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
