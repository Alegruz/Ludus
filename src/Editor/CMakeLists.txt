
set(PLATFORM_SOURCES "Platform/${PLATFORM_FOLDER}/Main.cpp")

if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE true)
endif()

add_executable(LudusEditor ${PLATFORM_SOURCES})

# Set output directory for the executable
set_target_properties(LudusEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

target_precompile_headers(LudusEditor PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Editor/Pch.hpp>"
    )

target_include_directories(LudusEditor
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

target_link_libraries(LudusEditor
    PRIVATE
    Ludus
    )

# Install rules for distribution
install(TARGETS LudusEditor
    RUNTIME DESTINATION .
)

# Handle runtime DLLs on Windows
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:LudusEditor>
        DESTINATION .
        COMPONENT Runtime
    )
endif()