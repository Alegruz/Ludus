@echo off
REM This batch script fetches the latest volk tag and configures CMake with it

REM Get the latest volk tag using the Python script
for /f "delims=" %%t in ('python tools/get_latest_volk_tag.py') do set VOLK_TAG=%%t

REM Optionally, get the latest Vulkan-Headers tag as well (currently hardcoded)
set VULKAN_HEADERS_TAG=v1.3.275

REM Run CMake with the latest volk tag
cmake -DCMAKE_BUILD_TYPE=Debug -DGRAPHICS_API=Vulkan -DVOLK_TAG=%VOLK_TAG% -DVULKAN_HEADERS_TAG=%VULKAN_HEADERS_TAG% %*
