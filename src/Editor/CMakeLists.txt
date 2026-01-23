
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

# Copy runtime DLLs (including external dependencies like mimalloc) on Windows
if(WIN32)
    add_custom_command(TARGET LudusEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:LudusEditor>" "${CMAKE_SOURCE_DIR}/"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    # Explicitly copy mimalloc DLLs if mimalloc is enabled
    if(TARGET mimalloc AND ENABLE_MIMALLOC)
        add_custom_command(TARGET LudusEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect.dll" "${CMAKE_SOURCE_DIR}/"
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
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_RUNTIME_DLL}" "${CMAKE_SOURCE_DIR}/"
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