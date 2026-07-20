add_executable(ludus_smoke)
target_sources(ludus_smoke PRIVATE main.cpp)
target_link_libraries(ludus_smoke PRIVATE Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_smoke)
