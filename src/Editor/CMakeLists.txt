if(WIN32)
    set(PLATFORM_SOURCES "Platform/${PLATFORM_FOLDER}/Main.cpp")
    set(CMAKE_WIN32_EXECUTABLE true)
else()
    message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_NAME} not supported")
endif()

add_executable(Editor ${PLATFORM_SOURCES})

target_precompile_headers(Editor PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Editor/Pch.hpp>"
    )

target_link_libraries(Editor
    PRIVATE
    Ludus
    )

# Copy the built executable into the repo root after every build
set(_dst "${CMAKE_SOURCE_DIR}/${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "dst: ${_dst}")
add_custom_command(
    TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Editor>" "${_dst}"
)

message(STATUS "COPY DIR ${CMAKE_CURRENT_LIST_DIR}/${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
if(LUDUS_WINDOWS)
    # Copy all DLLs from build directory to output directory
    file(GLOB DLL_FILES "${CMAKE_BINARY_DIR}/*.dll")
    foreach(DLL_FILE ${DLL_FILES})
        add_custom_command(TARGET Editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                "${CMAKE_CURRENT_LIST_DIR}/"
        )
    endforeach()
else()
    # Copy all shared libraries (.so) from build directory to output directory
    file(GLOB SO_FILES "${CMAKE_BINARY_DIR}/*.so")
    foreach(SO_FILE ${SO_FILES})
        add_custom_command(TARGET Editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SO_FILE}"
                "${CMAKE_CURRENT_LIST_DIR}/"
        )
    endforeach()
endif()