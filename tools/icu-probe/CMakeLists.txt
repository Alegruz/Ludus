# Private L0 experiment. Never install/export these targets or discover ICU in
# ordinary SDK builds. Upstream data tools run separately during bootstrap.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
if(LUDUS_COMPILED_LOG_LEVEL GREATER 2)
    message(FATAL_ERROR "L0 verification needs Info diagnostics; configure with -DLUDUS_COMPILED_LOG_LEVEL=2")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/icu-probe" verify
    RESULT_VARIABLE icu_verified
)
if(NOT icu_verified EQUAL 0)
    message(FATAL_ERROR "Prepare the pinned ICU sources, data and corpus with scripts/icu-probe bootstrap, then cook")
endif()

set(icu_source "${PROJECT_SOURCE_DIR}/out/icu-probe/source")
set(icu_generated "${PROJECT_SOURCE_DIR}/out/icu-probe/generated")
file(READ "${PROJECT_SOURCE_DIR}/config/icu_toolchain.json" icu_pin)
string(JSON icu_configuration_count LENGTH "${icu_pin}" configuration)
math(EXPR icu_configuration_last "${icu_configuration_count} - 1")
set(icu_configuration)
foreach(index RANGE ${icu_configuration_last})
    string(JSON definition GET "${icu_pin}" configuration ${index})
    list(APPEND icu_configuration "${definition}")
endforeach()

add_custom_target(ludus_icu_probe_verify
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/icu-probe" verify)

# Compile every upstream translation unit with the Ludus no-exception, sanitizer
# and flavor policy. The pinned UCONFIG switches empty excluded services; the
# static link keeps only what the probe references.
file(GLOB icu_common_sources CONFIGURE_DEPENDS "${icu_source}/common/*.cpp")
file(GLOB icu_i18n_sources CONFIGURE_DEPENDS "${icu_source}/i18n/*.cpp")
add_library(ludus_icu_probe_common STATIC EXCLUDE_FROM_ALL ${icu_common_sources} "${icu_source}/stubdata/stubdata.cpp")
target_include_directories(ludus_icu_probe_common PRIVATE "${icu_source}/common")
target_include_directories(ludus_icu_probe_common SYSTEM INTERFACE "${icu_source}/common")
target_compile_definitions(ludus_icu_probe_common PRIVATE U_COMMON_IMPLEMENTATION=1 PUBLIC ${icu_configuration})
add_library(ludus_icu_probe_i18n STATIC EXCLUDE_FROM_ALL ${icu_i18n_sources})
target_include_directories(ludus_icu_probe_i18n PRIVATE "${icu_source}/i18n")
target_include_directories(ludus_icu_probe_i18n SYSTEM INTERFACE "${icu_source}/i18n")
target_compile_definitions(ludus_icu_probe_i18n PRIVATE U_I18N_IMPLEMENTATION=1)
target_link_libraries(ludus_icu_probe_i18n PUBLIC ludus_icu_probe_common)
foreach(target ludus_icu_probe_common ludus_icu_probe_i18n)
    add_dependencies(${target} ludus_icu_probe_verify)
    target_link_libraries(${target} PRIVATE ludus_project_options)
    target_compile_options(${target} PRIVATE -w)
    set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endforeach()

add_executable(ludus_icu_probe main.cpp "${icu_generated}/icu_data.cpp")
target_include_directories(ludus_icu_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${icu_generated}")
# Upstream builds its own deprecated API; Ludus code must not depend on it.
string(JSON icu_consumer_configuration GET "${icu_pin}" consumer_configuration 0)
target_compile_definitions(ludus_icu_probe PRIVATE "${icu_consumer_configuration}")
target_link_libraries(ludus_icu_probe PRIVATE ludus_icu_probe_i18n Ludus::FoundationLogging Ludus::FoundationStrings
    Ludus::FoundationTime)
ludus_apply_project_defaults(ludus_icu_probe)
# The size baseline links the same Ludus closure without ICU or its data.
add_executable(ludus_icu_probe_baseline baseline.cpp)
target_link_libraries(ludus_icu_probe_baseline PRIVATE Ludus::FoundationLogging Ludus::FoundationStrings
    Ludus::FoundationTime)
ludus_apply_project_defaults(ludus_icu_probe_baseline)
configure_file("${icu_source}/LICENSE" ICU-LICENSE.txt COPYONLY)

if(EMSCRIPTEN)
    foreach(target ludus_icu_probe ludus_icu_probe_baseline)
        target_compile_options(${target} PRIVATE -g0)
        target_link_options(${target} PRIVATE -fno-exceptions -g0 -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
            -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
    endforeach()
endif()

enable_testing()
add_test(NAME ludus_icu_probe
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify.py"
        "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:ludus_icu_probe>"
        "$<TARGET_FILE:ludus_icu_probe_baseline>" "${PROJECT_BINARY_DIR}/compile_commands.json")
set_tests_properties(ludus_icu_probe PROPERTIES LABELS "localization;build-contract" TIMEOUT 600)
