# Platform library
add_library(LudusPlatform SHARED)

# Resolve platform-specific source up front so configuration fails fast if a file is missing
set(LUDUS_PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${PLATFORM_FOLDER}/Common.cpp")

if(NOT EXISTS "${LUDUS_PLATFORM_SOURCE}")
    message(FATAL_ERROR "Expected platform source missing: ${LUDUS_PLATFORM_SOURCE}")
endif()

target_sources(LudusPlatform PRIVATE "${LUDUS_PLATFORM_SOURCE}")

if(WIN32)
    # Automatically export all symbols on Windows to generate import library
    set_target_properties(LudusPlatform PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_precompile_headers(LudusPlatform PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Engine/Platform/Pch.hpp>"
    )

target_include_directories(LudusPlatform
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LudusPlatform
    PUBLIC
    LudusCore
    LudusBuildSettings
)

# Explicit mimalloc linkage if available
if(TARGET mimalloc)
    target_link_libraries(LudusPlatform PRIVATE mimalloc)
    add_dependencies(LudusPlatform mimalloc)
endif()

# Set DLL export macro
target_compile_definitions(LudusPlatform
    PRIVATE
        LUDUS_PLATFORM_EXPORTS
)

# Set output directory
set_target_properties(LudusPlatform PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
)