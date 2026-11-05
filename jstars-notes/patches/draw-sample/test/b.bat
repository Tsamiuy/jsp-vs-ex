@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
clang-cl /nologo /O2 /Zi /W3 test_ds.c /Fetest_ds.exe /link winmm.lib
