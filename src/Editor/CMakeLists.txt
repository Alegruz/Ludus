
set(PLATFORM_SOURCES "Platform/${PLATFORM_FOLDER}/Main.cpp")

if(WIN32)
    set(CMAKE_WIN32_EXECUTABLE true)
endif()

add_executable(LudusEditor ${PLATFORM_SOURCES})

# Set output directory for the executable to build/bin (multi-config safe)
# This avoids polluting the source root with build artifacts
set_target_properties(LudusEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

# Copy runtime DLLs (including external dependencies like mimalloc) on Windows
if(WIN32)
    add_custom_command(TARGET LudusEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:LudusEditor>" "${CMAKE_BINARY_DIR}/bin/"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    # Explicitly copy mimalloc DLLs if mimalloc is enabled
    if(TARGET mimalloc AND ENABLE_MIMALLOC)
        add_custom_command(TARGET LudusEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect.dll" "${CMAKE_BINARY_DIR}/bin/"
            VERBATIM
        )
    endif()
    # Copy AddressSanitizer runtime DLL when sanitizers are enabled
    if(ENABLE_SANITIZERS AND MSVC)
        # The ASan runtime lives under the VS LLVM tools directory; locate it instead of hardcoding a version-specific path
        set(ASAN_DLL_NAMES
            clang_rt.asan_dbg_dynamic-x86_64.dll
            clang_rt.asan_dynamic-x86_64.dll
        )

        # Common Visual Studio locations (VS 2022 first, then 2019 fallbacks)
        set(ASAN_HINT_DIRS
            "$ENV{VCToolsInstallDir}/../Llvm/x64/bin"
            "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin"
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin"
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Tools/Llvm/x64/bin"
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/Llvm/x64/bin"
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/Llvm/x64/bin"
        )

        find_file(ASAN_RUNTIME_DLL
            NAMES ${ASAN_DLL_NAMES}
            HINTS ${ASAN_HINT_DIRS}
            PATH_SUFFIXES bin
        )

        if(ASAN_RUNTIME_DLL)
            add_custom_command(TARGET LudusEditor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_RUNTIME_DLL}" "${CMAKE_BINARY_DIR}/bin/"
                VERBATIM
            )
        else()
            message(WARNING "ENABLE_SANITIZERS is ON, but the ASan runtime DLL was not found. Skipping copy; ensure LLVM/clang tools are installed in Visual Studio.")
        endif()
    endif()
endif()

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

# Generate debug symbols for MSVC
if(MSVC)
    target_compile_options(LudusEditor PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_link_options(LudusEditor PRIVATE $<$<CONFIG:Debug>:/DEBUG>)
endif()

# Install rules for distribution (puts exe in root of install prefix)
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