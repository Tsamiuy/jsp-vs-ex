@echo off
rem Runs the installed game with a scripted pad route: run-route.cmd ROUTE.txt INSTALL_FOLDER
rem The route file holds comment lines (#) and one line of "time:mask[:polls]" events; times count display flips.
setlocal
if "%~2"=="" (echo usage: run-route.cmd ROUTE.txt INSTALL_FOLDER & exit /b 2)
if not exist "%~1" (echo route file not found: %~1 & exit /b 2)
if not exist "%~2\jsp-launcher.exe" (echo no jsp-launcher.exe in %~2 & exit /b 2)
set "PAD_SCRIPT="
for /f "usebackq eol=# delims=" %%L in ("%~1") do set "PAD_SCRIPT=%%L"
if not defined PAD_SCRIPT (echo no route line in %~1 & exit /b 2)
set "PAD_CLOCK=flips"
"%~2\jsp-launcher.exe"
