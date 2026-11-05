@echo off
rem Configure and build the J-Stars port with clang-cl + Ninja.
rem   build.bat            configure (first time) and build
rem   build.bat 4          same, but with 4 parallel compiles (default 6)
setlocal
set JOBS=%1
if "%JOBS%"=="" set JOBS=6

set PORT=%~dp0
set SDK=%PORT%..\..\ps3recomp-src
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
rem Python: set PY to the full path of python.exe before calling this script,
rem or leave it unset to use the first python.exe found on PATH.
if not defined PY for /f "delims=" %%P in ('where python 2^>nul') do if not defined PY set "PY=%%P"
if not defined PY echo Python not found: set PY to the full path of python.exe & exit /b 1

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem vcvars does not reliably add VS's bundled CMake/Ninja or LLVM, so add them here.
set CMAKEDIR=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake
set PATH=%VS%\VC\Tools\Llvm\x64\bin;%CMAKEDIR%\CMake\bin;%CMAKEDIR%\Ninja;%PATH%

if not exist "%PORT%build\build.ninja" (
    cmake -S "%PORT%." -B "%PORT%build" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
        -DPS3RECOMP_DIR="%SDK%" ^
        -DRECOMP_DIR="%PORT%recompiled" ^
        -DPython3_EXECUTABLE="%PY%" || exit /b 1
)
cmake --build "%PORT%build" -j %JOBS%
