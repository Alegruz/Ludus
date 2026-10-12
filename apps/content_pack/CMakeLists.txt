add_executable(ludus_content_validate main.cpp)
target_link_libraries(ludus_content_validate PRIVATE Ludus::AudioContent Ludus::FoundationLogging)
ludus_apply_project_defaults(ludus_content_validate)

if(LUDUS_BUILD_TESTS)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ludus_content_validate_mesh
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mesh_root.py"
            "$<TARGET_FILE:ludus_content_validate>")
endif()
