# Exercise the same public solver corpus as the installed native consumer.
add_executable(ludus_web_fluid_probe main.cpp ../../tests/sdk_consumer/fluid.cpp)
target_link_libraries(ludus_web_fluid_probe PRIVATE Ludus::PhysicsFluid)
ludus_apply_project_defaults(ludus_web_fluid_probe)
target_link_options(ludus_web_fluid_probe PRIVATE
    -fno-exceptions -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
set_target_properties(ludus_web_fluid_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
add_test(NAME ludus_web_fluid_corpus
    COMMAND "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:ludus_web_fluid_probe>")
set_tests_properties(ludus_web_fluid_corpus PROPERTIES LABELS "web;fluid" TIMEOUT 120)
