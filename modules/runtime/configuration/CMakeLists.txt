add_library(ludus_runtime_configuration STATIC)
add_library(Ludus::RuntimeConfiguration ALIAS ludus_runtime_configuration)
set_target_properties(ludus_runtime_configuration PROPERTIES EXPORT_NAME RuntimeConfiguration)
target_sources(ludus_runtime_configuration PRIVATE src/host_options.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/runtime/configuration/host_options.hpp)
target_link_libraries(ludus_runtime_configuration PUBLIC Ludus::FoundationConfig)
ludus_apply_project_defaults(ludus_runtime_configuration)
install(TARGETS ludus_runtime_configuration EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
