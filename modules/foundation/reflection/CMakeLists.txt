add_library(ludus_foundation_reflection STATIC src/schema.cpp)
add_library(Ludus::FoundationReflection ALIAS ludus_foundation_reflection)
set_target_properties(ludus_foundation_reflection PROPERTIES EXPORT_NAME FoundationReflection)
target_sources(ludus_foundation_reflection PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/reflection/schema.hpp)
target_link_libraries(ludus_foundation_reflection PUBLIC Ludus::FoundationBase PRIVATE Ludus::FoundationMath Ludus::FoundationParsing)
set_target_properties(ludus_foundation_reflection PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
ludus_apply_project_defaults(ludus_foundation_reflection)
install(TARGETS ludus_foundation_reflection EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
