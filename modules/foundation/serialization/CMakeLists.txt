add_library(ludus_foundation_serialization STATIC src/json.cpp)
add_library(Ludus::FoundationSerialization ALIAS ludus_foundation_serialization)
set_target_properties(ludus_foundation_serialization PROPERTIES EXPORT_NAME FoundationSerialization)
target_sources(ludus_foundation_serialization PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/serialization/json.hpp)
target_link_libraries(ludus_foundation_serialization PUBLIC Ludus::FoundationReflection Ludus::FoundationMemory
    PRIVATE Ludus::FoundationParsingJson Ludus::FoundationMath)
set_target_properties(ludus_foundation_serialization PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
ludus_apply_project_defaults(ludus_foundation_serialization)
install(TARGETS ludus_foundation_serialization EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(LUDUS_BUILD_TESTS)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ludus_reflection_generator_tests COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/reflection/test_generate.py")
    set_tests_properties(ludus_reflection_generator_tests PROPERTIES LABELS "unit;reflection;codegen" TIMEOUT 60)
endif()
