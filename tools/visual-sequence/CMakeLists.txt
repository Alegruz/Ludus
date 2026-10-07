find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
# Private S3 conformance and local authoring owner; never installed/exported.
if(WIN32 AND NOT EMSCRIPTEN)
    message(FATAL_ERROR "S3's local authoring bridge currently requires a POSIX host; its interpreter conformance also runs on Wasm.")
endif()
add_executable(ludus_visual_sequence bridge.cpp)
target_link_libraries(ludus_visual_sequence PRIVATE ludus_script_session_fixture)
ludus_apply_project_defaults(ludus_visual_sequence)
if(EMSCRIPTEN)
    target_compile_options(ludus_visual_sequence PRIVATE -g0 -sSUPPORT_LONGJMP=wasm)
    target_link_options(ludus_visual_sequence PRIVATE -g0 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=1 -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2 -sMODULARIZE=1
        -sEXPORT_NAME=createVisualSequence "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']")
endif()
add_test(NAME ludus_visual_sequence COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/acceptance.py" "$<TARGET_FILE:ludus_visual_sequence>"
    "${CMAKE_CROSSCOMPILING_EMULATOR}")
set_tests_properties(ludus_visual_sequence PROPERTIES TIMEOUT 120 LABELS "scripting;visual")
add_test(NAME ludus_visual_sequence_owner COMMAND "${Python3_EXECUTABLE}" -W error::ResourceWarning
    -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")
set_tests_properties(ludus_visual_sequence_owner PROPERTIES TIMEOUT 120 LABELS "scripting;visual;tooling"
    ENVIRONMENT "LUDUS_S3_TEST_EXECUTABLE=$<TARGET_FILE:ludus_visual_sequence>;LUDUS_S3_NODE=${CMAKE_CROSSCOMPILING_EMULATOR}")
