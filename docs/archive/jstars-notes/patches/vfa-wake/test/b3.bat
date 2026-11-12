@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"


clang-cl /nologo /O2 test_vfa_queue.c /Fetest_vfa_queue.exe
