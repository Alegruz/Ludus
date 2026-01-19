add_library(Ludus SHARED Dummy.cpp)

if(WIN32)
    # Automatically export all symbols on Windows to generate import library
    set_target_properties(Ludus PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_link_libraries(Ludus
    PRIVATE
    LudusBuildSettings
    )