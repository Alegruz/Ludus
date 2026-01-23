add_executable(LudusTests
    Main.cpp
    ${CMAKE_SOURCE_DIR}/src/Engine/Core/CoreTests.cpp
)

# Override global WIN32_EXECUTABLE setting - tests should use console subsystem
set_target_properties(LudusTests PROPERTIES
    WIN32_EXECUTABLE OFF
)

# Set output directory for the executable (multi-config safe)
set_target_properties(LudusTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

target_precompile_headers(LudusTests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/include/Ludus/Engine/Core/Pch.hpp>"
)

target_include_directories(LudusTests
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LudusTests
    PRIVATE
    Ludus
)

# cppcheck occasionally fails on the tests target in CI; skip to keep analysis on engine code.
if(ENABLE_CPPCHECK)
    set_target_properties(LudusTests PROPERTIES CXX_CPPCHECK "")
endif()

# Handle runtime DLLs on Windows
if(WIN32)
    add_custom_command(TARGET LudusTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:LudusTests>" "${CMAKE_BINARY_DIR}/bin/"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    # Explicitly copy mimalloc DLLs if mimalloc is enabled
    if(TARGET mimalloc AND ENABLE_MIMALLOC)
        add_custom_command(TARGET LudusTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect.dll" "${CMAKE_BINARY_DIR}/bin/"
            VERBATIM
        )
    endif()
    # Copy AddressSanitizer runtime DLL when sanitizers are enabled
    if(ENABLE_SANITIZERS AND MSVC)
        set(ASAN_DLL_NAMES
            clang_rt.asan_dbg_dynamic-x86_64.dll
            clang_rt.asan_dynamic-x86_64.dll
        )

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
            add_custom_command(TARGET LudusTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_RUNTIME_DLL}" "${CMAKE_BINARY_DIR}/bin/"
                VERBATIM
            )
        else()
            message(WARNING "ENABLE_SANITIZERS is ON, but the ASan runtime DLL was not found. Skipping copy; ensure LLVM/clang tools are installed in Visual Studio.")
        endif()
    endif()
endif()

# Install rules for distribution
install(TARGETS LudusTests
    RUNTIME DESTINATION bin
)
