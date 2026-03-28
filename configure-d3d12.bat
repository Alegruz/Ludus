@echo off
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\Users\jinju\Documents\Ludus
cmake --preset ninja_msvc-debug-d3d12
