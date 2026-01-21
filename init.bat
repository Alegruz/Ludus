@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Ludus Project Initialization
echo ========================================
echo.

REM Download and extract RadDbg
echo Downloading RadDbg...
set RADDBG_URL=https://github.com/EpicGamesExt/raddebugger/releases/download/v0.9.24-alpha/raddbg.zip
set RADDBG_ZIP=raddbg.zip
set RADDBG_DIR=raddbg

REM Download using PowerShell
powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%RADDBG_URL%' -OutFile '%RADDBG_ZIP%'}"

if !errorlevel! neq 0 (
    echo Failed to download RadDbg
    exit /b 1
)

echo RadDbg downloaded successfully!

REM Remove existing raddbg directory if it exists
if exist "%RADDBG_DIR%" (
    echo Removing existing raddbg directory...
    rmdir /s /q "%RADDBG_DIR%"
)

REM Extract the zip file
echo Extracting RadDbg...
powershell -Command "& {Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%RADDBG_ZIP%', '%RADDBG_DIR%')}"

if !errorlevel! neq 0 (
    echo Failed to extract RadDbg
    exit /b 1
)

echo RadDbg extracted successfully!

REM Clean up the zip file
echo Cleaning up...
del "%RADDBG_ZIP%"

echo.
echo ========================================
echo Initialization Complete!
echo ========================================
echo RadDbg installed to: %RADDBG_DIR%
echo.

REM Check and install LLVM (clang-tidy, clang-format)
echo Checking for LLVM tools (clang-tidy, clang-format)...
where clang-tidy >nul 2>nul
if errorlevel 1 (
    echo clang-tidy not found. Attempting installation...
    where choco >nul 2>nul
    if errorlevel 1 (
        echo.
        echo Chocolatey not found. Install it from https://chocolatey.org/install
        echo Then run: choco install llvm -y
        goto :skip_llvm_install
    )
    
    echo Installing LLVM via Chocolatey ^(requires admin privileges^)...
    choco install llvm -y
    if errorlevel 1 (
        echo.
        echo Failed to install LLVM. You may need to run this script as Administrator.
        echo Or install manually from: https://llvm.org/builds/
    ) else (
        echo LLVM installed successfully!
    )
) else (
    echo clang-tidy found!
)

:skip_llvm_install
echo.
echo ========================================
echo Setup Complete!
echo ========================================
echo.

endlocal
