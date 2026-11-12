@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
set R=..\vfetch
clang-cl /nologo /O2 /std:c17 -D_CRT_SECURE_NO_WARNINGS -I%R%\include -I%R%\libs\video -I%R%\runtime test_fifofast.c %R%\libs\video\rsx_commands.c /Fetest_fifofast.exe /link /FORCE:UNRESOLVED 2>&1 | findstr /i "error unresolved"
