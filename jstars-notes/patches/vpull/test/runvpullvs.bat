@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
clang-cl /nologo /O1 /std:c17 -D_CRT_SECURE_NO_WARNINGS -I..\vfetch\include -I..\vfetch\libs\video test_vpull_vs.c ..\vfetch\libs\video\rsx_vp_decompiler.c d3dcompiler.lib dxguid.lib /Fetest_vpull_vs.exe 2>&1 | findstr /i "error"
