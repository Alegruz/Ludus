find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/script-session" verify
    RESULT_VARIABLE verified)
if(NOT verified EQUAL 0)
    message(FATAL_ERROR "Prepare S2 using scripts/script-session bootstrap, then cook")
endif()
add_custom_target(ludus_script_session_verify
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/script-session" verify)
add_library(ludus_script_session_fixture STATIC session.cpp control.cpp
    "${PROJECT_SOURCE_DIR}/out/script-interaction/cooked/bindings.cpp")
target_include_directories(ludus_script_session_fixture PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/out/script-session/cooked"
    "${PROJECT_SOURCE_DIR}/modules/runtime/scripting/src")
target_link_libraries(ludus_script_session_fixture PUBLIC ludus_script_fixture ludus_runtime_scripting
    Luau.VM Ludus::FoundationParsingJson Ludus::FoundationMemory Ludus::FoundationMath)
ludus_apply_project_defaults(ludus_script_session_fixture)
add_dependencies(ludus_script_session_fixture ludus_script_session_verify)
add_executable(ludus_script_session main.cpp acceptance.cpp)
target_link_libraries(ludus_script_session PRIVATE ludus_script_session_fixture Ludus::FoundationLogging)
ludus_apply_project_defaults(ludus_script_session)
if(EMSCRIPTEN)
    foreach(target ludus_yyjson ludus_foundation_base ludus_foundation_memory ludus_foundation_containers
        ludus_foundation_math ludus_foundation_time ludus_foundation_hash ludus_foundation_strings
        ludus_foundation_logging ludus_foundation_parsing ludus_foundation_parsing_json ludus_gameplay_world)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()
    foreach(target ludus_script_session_fixture ludus_script_session)
        target_compile_options(${target} PRIVATE -g0 -sSUPPORT_LONGJMP=wasm)
    endforeach()
    target_link_options(ludus_script_session PRIVATE -g0 -sEXIT_RUNTIME=1 -sENVIRONMENT=web,node
        -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2 "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']")
    configure_file(shell.html index.html COPYONLY)
endif()
enable_testing()
add_test(NAME ludus_script_session COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/verify.py" "${CMAKE_CROSSCOMPILING_EMULATOR}"
    "$<TARGET_FILE:ludus_script_session>")
set_tests_properties(ludus_script_session PROPERTIES TIMEOUT 120 LABELS scripting)
if(NOT EMSCRIPTEN AND TARGET ludus_game_host_app)
    foreach(target ludus_yyjson ludus_foundation_base ludus_foundation_memory ludus_foundation_containers
        ludus_foundation_math ludus_foundation_time ludus_foundation_hash ludus_foundation_strings
        ludus_foundation_logging ludus_foundation_parsing ludus_foundation_parsing_json ludus_gameplay_world)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()
    foreach(target ludus_script_session_fixture ludus_script_fixture ludus_runtime_scripting Luau.VM Luau.Common)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    endforeach()
    foreach(variant A B)
        add_library(ludus_script_session_module_${variant} MODULE module.cpp)
        target_link_libraries(ludus_script_session_module_${variant} PRIVATE ludus_script_session_fixture Ludus::GameApi)
        target_compile_definitions(ludus_script_session_module_${variant} PRIVATE
            LUDUS_FIXTURE_IDENTITY="${LUDUS_GAME_COMPATIBILITY_IDENTITY}")
        ludus_apply_project_defaults(ludus_script_session_module_${variant})
        set_target_properties(ludus_script_session_module_${variant} PROPERTIES
            CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
        if(APPLE)
            target_link_options(ludus_script_session_module_${variant} PRIVATE "-Wl,-exported_symbol,_LudusGetGameApi")
        else()
            target_link_options(ludus_script_session_module_${variant} PRIVATE "-Wl,--exclude-libs,ALL")
        endif()
        add_dependencies(ludus_script_session ludus_script_session_module_${variant})
    endforeach()
    add_dependencies(ludus_script_session ludus_game_host_app)
    add_test(NAME ludus_script_session_host COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/host_acceptance.py" "$<TARGET_FILE:ludus_game_host_app>"
        "$<TARGET_FILE:ludus_script_session_module_A>" "$<TARGET_FILE:ludus_script_session_module_B>")
    set_tests_properties(ludus_script_session_host PROPERTIES TIMEOUT 120 LABELS "scripting;reload")
endif()
