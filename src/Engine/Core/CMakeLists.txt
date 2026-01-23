# Core library
add_library(LudusCore SHARED)

# Explicitly list source files for better dependency tracking
target_sources(LudusCore PRIVATE
    CommandLineOptions.cpp
    CommandLineManager.cpp
    Random.cpp
    CoreTests.cpp
    Container/String.cpp
)

if(WIN32)
    # Automatically export all symbols on Windows to generate import library
    set_target_properties(LudusCore PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_precompile_headers(LudusCore PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Engine/Core/Pch.hpp>"
    )

target_include_directories(LudusCore
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LudusCore
    PUBLIC
    LudusBuildSettings
)

# Explicit mimalloc linkage if available
if(TARGET mimalloc)
    target_link_libraries(LudusCore PRIVATE mimalloc)
    add_dependencies(LudusCore mimalloc)
endif()

# Set DLL export macro
target_compile_definitions(LudusCore
    PRIVATE
        LUDUS_CORE_EXPORTS
)

# Set output directory
set_target_properties(LudusCore PROPERTIES
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