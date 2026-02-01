@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
pushd "%ROOT%"

set PYTHON=
for %%P in (python python3) do (
    where %%P >nul 2>nul && set PYTHON=%%P && goto :found_python
)

:found_python
if "%PYTHON%"=="" (
    echo Python not found. Attempting install...
    where winget >nul 2>nul
    if !errorlevel! == 0 (
        winget install --id Python.Python.3.12 -e
    ) else (
        where choco >nul 2>nul
        if !errorlevel! == 0 (
            choco install python -y
        ) else (
            echo No package manager found. Install Python 3.10+ from https://www.python.org/downloads/
        )
    )
)

set PYTHON=
for %%P in (python python3) do (
    where %%P >nul 2>nul && set PYTHON=%%P && goto :run_onboard
)

echo Python still not found. Aborting.
goto :done

:run_onboard
%PYTHON% tools\onboard\onboard.py %*

:done
popd
endlocal
