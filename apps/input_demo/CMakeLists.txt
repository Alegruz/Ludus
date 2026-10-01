add_executable(ludus_input_demo)
target_sources(ludus_input_demo PRIVATE main.cpp)
target_link_libraries(
    ludus_input_demo
    PRIVATE
        Ludus::FoundationBase
        Ludus::FoundationLogging
        Ludus::Input
        Ludus::Platform
)
ludus_apply_project_defaults(ludus_input_demo)
