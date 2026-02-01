@echo off
REM This batch script fetches the latest volk tag and configures CMake with it
REM Note: Volk uses vulkan-sdk version tags (vulkan-sdk-X.Y.Z.W format)

REM Get the latest volk tag using the Python script
for /f "delims=" %%t in ('python tools/get_latest_volk_tag.py') do set VOLK_TAG=%%t

REM Extract Vulkan version from volk tag for Vulkan-Headers (vulkan-sdk-1.4.335.0 -> v1.4.335)
for /f "tokens=2 delims=-" %%v in ("!VOLK_TAG!") do (
    for /f "tokens=1,2,3 delims=." %%a in ("%%v") do (
        set VULKAN_HEADERS_TAG=v%%a.%%b.%%c
    )
)

REM Run CMake with the latest volk tag
cmake -DCMAKE_BUILD_TYPE=Debug -DGRAPHICS_API=Vulkan -DVOLK_TAG=%VOLK_TAG% -DVULKAN_HEADERS_TAG=%VULKAN_HEADERS_TAG% %*
