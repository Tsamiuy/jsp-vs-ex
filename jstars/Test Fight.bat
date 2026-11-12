@echo off
rem Opens the game straight into a Training match, without touching the menus.
rem   Test Fight.bat <fighter> <opponent> <support> [stage]     numbers, for example: Test Fight.bat 28 32 7
rem With no numbers it uses the first line of jstars\test_fight.txt. Normal play is not affected.
rem When the game closes, the session's log is kept in jstars\run\test_fights, named after the fighter and the time.
if "%~1"=="" (
    set /p AUTO_FIGHT=<"%~dp0test_fight.txt"
) else (
    set "AUTO_FIGHT=%*"
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1"
