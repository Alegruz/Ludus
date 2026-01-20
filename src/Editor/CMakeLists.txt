
set(PLATFORM_SOURCES "Platform/${PLATFORM_FOLDER}/Main.cpp")

if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE true)
endif()

add_executable(LudusEditor ${PLATFORM_SOURCES})

# Set output directory for the executable (multi-config safe)
set_target_properties(LudusEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}"
)

# Also copy/sync the built exe to the source root after build to ensure the launch task finds it.
add_custom_command(TARGET LudusEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:LudusEditor>" "${CMAKE_SOURCE_DIR}/LudusEditor.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:LudusCore>" "${CMAKE_SOURCE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:LudusPlatform>" "${CMAKE_SOURCE_DIR}/"
    COMMAND_EXPAND_LISTS
    VERBATIM
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