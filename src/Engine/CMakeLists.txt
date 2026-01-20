add_subdirectory(Core)
add_subdirectory(Platform)

# Umbrella library that includes all engine components
add_library(Ludus INTERFACE)

target_link_libraries(Ludus
    INTERFACE
    LudusPlatform
    LudusCore
)