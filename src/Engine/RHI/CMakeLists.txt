# RHI library
add_library(LudusRHI SHARED)

# Resolve platform-specific source up front so configuration fails fast if a file is missing
set(LUDUS_RHI_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Texture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Instance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Adapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Device.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CommandQueue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SwapChain.cpp"
    )

foreach(LUDUS_RHI_SOURCE IN LISTS LUDUS_RHI_SOURCES)
     if(NOT EXISTS "${LUDUS_RHI_SOURCE}")
         ludus_fatal_error("Expected rhi source missing: ${LUDUS_RHI_SOURCE}")
     endif()
endforeach()

target_sources(LudusRHI PRIVATE ${LUDUS_RHI_SOURCES})

if(WIN32)
    # Automatically export all symbols on Windows to generate import library
    set_target_properties(LudusRHI PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_precompile_headers(LudusRHI PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Engine/RHI/Pch.hpp>"
    )

target_include_directories(LudusRHI
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LudusRHI
    PUBLIC
    LudusCore
    LudusPlatform
    LudusBuildSettings
)

target_link_libraries(LudusRHI PRIVATE LudusVulkanDeps)

# Generate debug symbols for MSVC
if(MSVC)
    target_compile_options(LudusRHI PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_link_options(LudusRHI PRIVATE $<$<CONFIG:Debug>:/DEBUG>)
endif()

# Explicit mimalloc linkage if available
if(TARGET mimalloc)
    target_link_libraries(LudusRHI PRIVATE mimalloc)
    add_dependencies(LudusRHI mimalloc)
endif()

# Set output directory
set_target_properties(LudusRHI PROPERTIES
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
