@echo off
setlocal EnableExtensions

rem Ludus Windows one-command onboarding.
rem This file intentionally uses cmd.exe so it can be run from any terminal or
rem by double-clicking without changing the PowerShell execution policy.

set "LUDUS_ROOT=%~dp0"
set "LUDUS_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "LUDUS_VS_PATH="

if not exist "%LUDUS_VSWHERE%" goto :missing_msvc
for %%I in ("%LUDUS_VSWHERE%") do set "LUDUS_VSWHERE=%%~sI"

for /f "tokens=*" %%I in ('%LUDUS_VSWHERE% -latest -version [17.0^,18.0^^^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "LUDUS_VS_PATH=%%I"
if not defined LUDUS_VS_PATH goto :missing_msvc
if not exist "%LUDUS_VS_PATH%\Common7\Tools\VsDevCmd.bat" goto :missing_msvc

call "%LUDUS_VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 goto :vs_environment_failed

where py.exe >nul 2>nul
if not errorlevel 1 (
    py.exe -3 "%LUDUS_ROOT%scripts\python\engine.py" init %*
    set "LUDUS_EXIT_CODE=%ERRORLEVEL%"
    goto :finished
)

where python.exe >nul 2>nul
if not errorlevel 1 (
    python.exe "%LUDUS_ROOT%scripts\python\engine.py" init %*
    set "LUDUS_EXIT_CODE=%ERRORLEVEL%"
    goto :finished
)

echo.
echo ERROR: Python 3.10 or newer is required.
echo Install it from https://www.python.org/downloads/windows/ and enable
echo "Add python.exe to PATH", then run init.cmd again.
set "LUDUS_EXIT_CODE=1"
goto :finished

:missing_msvc
echo.
echo ERROR: Visual Studio 2022 C++ Build Tools were not found.
echo Install "Build Tools for Visual Studio 2022" with the
echo "Desktop development with C++" workload, then run init.cmd again.
echo https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
set "LUDUS_EXIT_CODE=1"
goto :finished

:vs_environment_failed
echo.
echo ERROR: Visual Studio 2022 could not initialize its x64 build environment.
set "LUDUS_EXIT_CODE=1"

:finished
if not defined LUDUS_EXIT_CODE set "LUDUS_EXIT_CODE=1"
echo.
if "%LUDUS_EXIT_CODE%"=="0" (
    echo Ludus Windows onboarding finished successfully.
) else (
    echo Ludus Windows onboarding failed with exit code %LUDUS_EXIT_CODE%.
)

rem Explorer launches cmd files with CMDLINE containing /c and the script path.
rem Keep failures visible in that case, while terminal invocations return directly.
echo(%CMDCMDLINE%| findstr /i /c:" /c " >nul 2>nul
if not errorlevel 1 if not "%LUDUS_EXIT_CODE%"=="0" pause

exit /b %LUDUS_EXIT_CODE%
