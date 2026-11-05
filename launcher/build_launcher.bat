@echo off
rem Builds "J-Stars Launcher.exe" in the PS3Recomp folder from JStarsLauncher.cs,
rem with the C# compiler that comes with Windows (.NET Framework 4). No installs.
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
"%CSC%" /nologo /target:winexe /optimize+ /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
  /out:"%~dp0..\J-Stars Launcher.exe" "%~dp0JStarsLauncher.cs"
if errorlevel 1 (echo Build failed. & pause & exit /b 1)
echo Built: %~dp0..\J-Stars Launcher.exe
