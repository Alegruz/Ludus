# DirectX 12 Agility SDK Integration

This document describes how the Ludus engine integrates and deploys the DirectX 12 Agility SDK.

## Overview

The DirectX 12 Agility SDK is a redistribution package that allows applications to use the latest D3D12 features independent of the OS-installed version. This enables:

- Access to new D3D12 features on older Windows versions
- App-local deployment of updated GPU drivers
- Better forward compatibility with future Windows updates

## Integration Method

The Ludus engine uses **Method C** from the [Microsoft DirectX 12 Agility SDK DevBlog](https://devblogs.microsoft.com/directx/directx-12-agility-sdk-1-4-10/):

1. **Automatic Download**: CMake fetches the NuGet package from nuget.org during configuration
2. **Extraction**: The .nupkg file is extracted as a .zip archive
3. **Include Path Management**: Agility SDK headers are included BEFORE Windows SDK headers (critical!)
4. **Runtime Deployment**: D3D12Core.dll and d3d12SDKLayers.dll are copied to app-local `D3D12/` directory

## CMake Integration

### FindD3D12AgilitySDK.cmake

Located in `cmake/FindD3D12AgilitySDK.cmake`, this module:

- **Downloads** the specified Agility SDK version from NuGet
- **Extracts** the archive to `./_deps/d3d12-agility-sdk/d3d12-sdk`
- **Creates** an INTERFACE library target `D3D12::D3D12`
- **Manages** include paths (Agility SDK first!)
- **Provides** runtime file copying via `d3d12_setup_runtime_files()` function

### Configuration Variables

```cmake
D3D12_AGILITY_SDK_VERSION    # SDK version (default: 1.4.10)
D3D12_AGILITY_SDK_DOWNLOAD_DIR  # Cache location (default: ./_deps/d3d12-agility-sdk)
```

To use a different SDK version:

```bash
cmake --preset ninja_msvc-debug-d3d12 -DD3D12_AGILITY_SDK_VERSION=1.4.11
```

## Build System Configuration

### RHI (Rendering Hardware Interface)

In `src/Engine/RHI/CMakeLists.txt`:

```cmake
elseif(GRAPHICS_API STREQUAL "D3D12")
    # Integrate DirectX 12 Agility SDK
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    include(FindD3D12AgilitySDK)
    
    # Link D3D12 Agility SDK
    target_link_libraries(LudusRHI PUBLIC D3D12::D3D12)
    
    # Copy Agility SDK runtime files to output directory
    d3d12_setup_runtime_files(LudusRHI ${CMAKE_BINARY_DIR}/bin)
endif()
```

This ensures:
- Only D3D12 builds trigger SDK setup
- Headers are in the correct include order
- Runtime DLLs are deployed to `bin/D3D12/`

## Agility SDK Version Management

The `D3D12SDKVersion` constant in `src/Engine/RHI/D3D12/Instance.cpp` **MUST match** the downloaded SDK version:

| Agility SDK Package Version | D3D12SDKVersion | Release Date |
|---------------------------|-----------------|-------------|
| 1.618.5                   | 618             | 12/5/2025   |
| 1.4.11                    | 614             | Earlier     |
| 1.4.10                    | 614             | Earlier     |
| 1.4.9                     | 612             | Earlier     |
| 1.4.8                     | 609             | Earlier     |
| 1.4.7                     | 607             | Earlier     |

Current: **v1.618.5 → SDK version 618** (Latest as of December 2025)

See [Agility SDK Release Notes](https://github.com/microsoft/DirectX-Headers/releases) for complete mapping.

## Runtime Deployment

### App-Local Directory Structure

After a D3D12 build, the output directory will contain:

```
bin/
├── LudusEditor.exe
├── LudusRHI.dll
└── D3D12/
    ├── D3D12Core.dll         ← Latest GPU driver/features
    └── d3d12SDKLayers.dll    ← Debug layers (if included)
```

The `D3D12SDKPath = ".\\D3D12\\"` setting in `Instance.cpp` tells the loader to use these local files.

### Debug Layers

To enable debug layers during development:

```cpp
// In D3D12 Initialize() implementation
#if defined(LUDUS_DEBUG)
    // Enable debug layer
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif
```

## Troubleshooting

### "Cannot find d3d12.h"

- **Cause**: CMake module not included or D3D12 preset not used
- **Fix**: Use a D3D12 preset: `cmake --preset ninja_msvc-debug-d3d12`

### "D3D12Core.dll not found at runtime"

- **Cause**: Runtime files not deployed to app directory
- **Fix**: Verify `bin/D3D12/D3D12Core.dll` exists; rebuild if needed

### "D3D12SDKVersion mismatch"

- **Cause**: Constant doesn't match downloaded SDK version
- **Fix**: Check `D3D12_AGILITY_SDK_VERSION` in CMake and update `D3D12SDKVersion` in Instance.cpp

### Slow initial build

- **Cause**: First CMake configuration downloads and extracts SDK (1-2 GB)
- **Fix**: Subsequent builds use cached files; first build may take 2-5 minutes depending on internet

## Microsoft References

- [Agility SDK Overview](https://devblogs.microsoft.com/directx/announcing-the-new-directx-12-agility-sdk/)
- [Agility SDK Dev Blog](https://devblogs.microsoft.com/directx/directx-12-agility-sdk-1-4-10/)
- [Release Notes & Versions](https://github.com/microsoft/DirectX-Headers/releases)
- [GitHub Repository](https://github.com/microsoft/DirectX-Specs)

## GitHub Workflows

All D3D12 CI configurations automatically:
1. Run `init.bat --install --preset ci_windows_msvc_debug_d3d12`
2. Configure with Agility SDK support enabled
3. Deploy runtime files as part of the build
4. Run on Windows latest (MSVC compiler only)

See `.github/workflows/ci-main.yml` and `.github/workflows/ci-pr.yml` for details.
