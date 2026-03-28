# FindD3D12AgilitySDK.cmake
# Fetches and integrates the DirectX 12 Agility SDK for app-local deployment
# This follows Microsoft's "Method C" approach from the Agility SDK devblog

include_guard(GLOBAL)

set(D3D12_AGILITY_SDK_VERSION "1.618.5" CACHE STRING "DirectX 12 Agility SDK version to fetch")
set(D3D12_AGILITY_SDK_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/d3d12-agility-sdk" CACHE PATH "Directory to download Agility SDK")

# Ensure the download directory exists
file(MAKE_DIRECTORY "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}")

set(D3D12_AGILITY_SDK_NUPKG "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/Microsoft.Direct3D.D3D12.${D3D12_AGILITY_SDK_VERSION}.nupkg")
set(D3D12_AGILITY_SDK_ZIP "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/agility.zip")
set(D3D12_AGILITY_SDK_PATH "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/d3d12-sdk")

# Check if SDK is already downloaded and extracted
if(NOT EXISTS "${D3D12_AGILITY_SDK_PATH}/include/d3d12.h")
    message(STATUS "Fetching DirectX 12 Agility SDK v${D3D12_AGILITY_SDK_VERSION}...")
    
    # Download the NuGet package
    if(NOT EXISTS "${D3D12_AGILITY_SDK_NUPKG}")
        message(STATUS "Downloading D3D12 Agility SDK NuGet package...")
        if(CMAKE_HOST_WIN32)
            # Use PowerShell for more reliable downloads on Windows
            execute_process(
                COMMAND powershell -NoProfile -Command "
                    $ProgressPreference = 'SilentlyContinue'
                    Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${D3D12_AGILITY_SDK_VERSION}' -OutFile '${D3D12_AGILITY_SDK_ZIP}'
                    if (Test-Path '${D3D12_AGILITY_SDK_ZIP}') { exit 0 } else { exit 1 }
                "
                RESULT_VARIABLE DOWNLOAD_RESULT
            )
        else()
            # Use cmake file download for Unix
            file(DOWNLOAD
                "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${D3D12_AGILITY_SDK_VERSION}"
                "${D3D12_AGILITY_SDK_ZIP}"
                SHOW_PROGRESS
                STATUS DOWNLOAD_STATUS
            )
            list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        endif()
        
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download D3D12 Agility SDK from NuGet. Check network connectivity and try again.")
        endif()
    else()
        # Rename .nupkg to .zip for extraction
        file(COPY "${D3D12_AGILITY_SDK_NUPKG}" DESTINATION "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}")
        file(RENAME "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/Microsoft.Direct3D.D3D12.${D3D12_AGILITY_SDK_VERSION}.nupkg"
                    "${D3D12_AGILITY_SDK_ZIP}")
    endif()
    
    # Extract the zip file (NuGet packages are zip, not tar.gz)
    message(STATUS "Extracting D3D12 Agility SDK...")
    if(CMAKE_HOST_WIN32)
        # Use .NET ZipFile class for reliable extraction (no module loading issues)
        execute_process(
            COMMAND powershell -NoProfile -Command "
                Add-Type -AssemblyName System.IO.Compression.FileSystem
                [System.IO.Compression.ZipFile]::ExtractToDirectory('${D3D12_AGILITY_SDK_ZIP}', '${D3D12_AGILITY_SDK_DOWNLOAD_DIR}')
            "
            RESULT_VARIABLE EXTRACT_RESULT
        )
    else()
        # Unix/macOS: use cmake -E tar (works with zip in cmake 3.18+)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${D3D12_AGILITY_SDK_ZIP}"
            WORKING_DIRECTORY "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
    endif()
    
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract D3D12 Agility SDK from ${D3D12_AGILITY_SDK_ZIP}")
    endif()
    
    # Find and rename extracted directory to standardized name
    file(GLOB EXTRACTED_DIR "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/build/native")
    if(EXTRACTED_DIR)
        file(RENAME "${EXTRACTED_DIR}" "${D3D12_AGILITY_SDK_PATH}")
    else()
        # Fallback: check if build/native already exists
        if(EXISTS "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/build/native")
            file(RENAME "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}/build/native" "${D3D12_AGILITY_SDK_PATH}")
        else()
            message(FATAL_ERROR "Could not find 'build/native' directory in extracted Agility SDK at ${D3D12_AGILITY_SDK_DOWNLOAD_DIR}")
        endif()
    endif()
    
    message(STATUS "DirectX 12 Agility SDK extracted to: ${D3D12_AGILITY_SDK_PATH}")
endif()

# Verify SDK structure
if(NOT EXISTS "${D3D12_AGILITY_SDK_PATH}/include/d3d12.h")
    # Clean up partial/corrupted download
    if(EXISTS "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}")
        file(REMOVE_RECURSE "${D3D12_AGILITY_SDK_DOWNLOAD_DIR}")
    endif()
    
    message(FATAL_ERROR 
        "D3D12 Agility SDK download/extraction failed.\n"
        "\n"
        "To recover:\n"
        "  1. Run: .\\init.bat --install\n"
        "  2. Retry: cmake --preset ninja_msvc-debug-d3d12\n"
        "\n"
        "If the problem persists, manually download from:\n"
        "  https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${D3D12_AGILITY_SDK_VERSION}\n"
    )
endif()

# Verify runtime DLLs are available
if(NOT EXISTS "${D3D12_AGILITY_SDK_PATH}/bin/x64/D3D12Core.dll")
    message(FATAL_ERROR 
        "D3D12 Agility SDK runtime DLL not found at ${D3D12_AGILITY_SDK_PATH}/bin/x64/D3D12Core.dll\n"
        "\n"
        "The SDK extraction was incomplete. To fix this:\n"
        "  1. Delete: ${D3D12_AGILITY_SDK_DOWNLOAD_DIR}\n"
        "  2. Run: .\\init.bat --install\n"
        "  3. Retry: cmake --preset ninja_msvc-debug-d3d12\n"
    )
endif()

# Set variables for use in target configuration
set(D3D12_AGILITY_SDK_INCLUDE_DIR "${D3D12_AGILITY_SDK_PATH}/include" CACHE PATH "D3D12 Agility SDK include directory" FORCE)
set(D3D12_AGILITY_SDK_BIN_DIR "${D3D12_AGILITY_SDK_PATH}/bin/x64" CACHE PATH "D3D12 Agility SDK bin directory" FORCE)

message(STATUS "D3D12 Agility SDK found at: ${D3D12_AGILITY_SDK_PATH}")
message(STATUS "  Include: ${D3D12_AGILITY_SDK_INCLUDE_DIR}")
message(STATUS "  Bin: ${D3D12_AGILITY_SDK_BIN_DIR}")

# Create imported library target
if(NOT TARGET D3D12::D3D12)
    add_library(D3D12::D3D12 INTERFACE IMPORTED)
    
    # Set include directories (MUST come before Windows SDK includes)
    # The Agility SDK headers take precedence over Windows SDK headers
    target_include_directories(D3D12::D3D12 INTERFACE
        "${D3D12_AGILITY_SDK_INCLUDE_DIR}"
    )
    
    # Link against system Windows SDK libraries (d3d12.lib comes from Windows SDK)
    # The Agility SDK provides headers and runtime DLLs, not the import library
    target_link_libraries(D3D12::D3D12 INTERFACE
        d3d12
        dxgi
        d3dcompiler
    )
endif()

# Create a target to copy Agility SDK runtime files to output directory
function(d3d12_setup_runtime_files TARGET_NAME OUTPUT_DIR)
    # Create D3D12 subdirectory in output
    set(D3D12_OUTPUT_DIR "${OUTPUT_DIR}/D3D12")
    
    # List of runtime files to copy
    set(RUNTIME_FILES
        "D3D12Core.dll"
        "d3d12SDKLayers.dll"
    )
    
    foreach(RUNTIME_FILE ${RUNTIME_FILES})
        set(SOURCE_FILE "${D3D12_AGILITY_SDK_BIN_DIR}/${RUNTIME_FILE}")
        if(EXISTS "${SOURCE_FILE}")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${D3D12_OUTPUT_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SOURCE_FILE}" "${D3D12_OUTPUT_DIR}/"
                COMMENT "Copying ${RUNTIME_FILE} to app-local D3D12 directory"
            )
        endif()
    endforeach()
endfunction()
