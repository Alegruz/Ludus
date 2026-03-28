add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(RHI)
add_subdirectory(Renderer)

# Umbrella library that includes all engine components
add_library(Ludus INTERFACE)

target_link_libraries(Ludus
    INTERFACE
    LudusRenderer
    LudusRHI
    LudusPlatform
    LudusCore
)