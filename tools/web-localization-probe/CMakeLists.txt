add_executable(ludus_web_localization_probe main.cpp)
add_dependencies(ludus_web_localization_probe ludus_localization_fixture)
target_include_directories(ludus_web_localization_probe PRIVATE
    "${PROJECT_BINARY_DIR}/modules/localization/generated")
target_link_libraries(ludus_web_localization_probe PRIVATE Ludus::Localization)
ludus_apply_project_defaults(ludus_web_localization_probe)
target_link_options(ludus_web_localization_probe PRIVATE
    -fno-exceptions -g2 -sENVIRONMENT=web,node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
set_target_properties(ludus_web_localization_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
enable_testing()
add_test(NAME ludus_web_localization_corpus COMMAND "${CMAKE_CROSSCOMPILING_EMULATOR}"
    "$<TARGET_FILE:ludus_web_localization_probe>")
set_tests_properties(ludus_web_localization_corpus PROPERTIES LABELS "web;localization" TIMEOUT 60)
