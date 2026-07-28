@echo off
cd /d "%~dp0"

rem ================================================================
rem Geometry Dash Wrapper 0.9.5-unified7-fix2-focused+logging1
rem
rem You can either:
rem   1. Put an APK beside this file as game.apk and double-click it, or
rem   2. Drag any .apk file onto RUN_AUTO.cmd.
rem ================================================================

rem API base. You may include http:// or https:// and a custom port.
rem Examples:
rem   www.boomlings.com/database
rem   http://game.example.com/server
set "GDPS_SERVER=www.boomlings.com/database"

rem Make exported icon ownership checks return true for this run.
rem This does not permanently write unlocks into the save file.
set "HACK_ICONS=false"

rem Unlock Creator access in spin-offs, including online tabs and My Levels.
set "FULL_BYPASS=true"

rem Force the highest exported graphics tier when the APK exposes the checks.
set "FORCE_HIGHEST_GRAPHICS=true"

rem Cap the music-reactive visual meter (0.00 to 1.00).
set "MUSIC_PULSE_MAX=0.30"

if "%~1"=="" (
    python run_auto.py
) else (
    if /I not "%~x1"==".apk" (
        echo.
        echo ERROR: Drag an Android .apk file onto RUN_AUTO.cmd.
        echo Selected file: %~f1
        echo.
        pause
        exit /b 2
    )
    python run_auto.py "%~f1"
)
set "GD_EXIT_CODE=%ERRORLEVEL%"
if not "%GD_EXIT_CODE%"=="0" (
    echo.
    echo Wrapper exited with code %GD_EXIT_CODE%.
    echo Check the newest folder under logs\ for run-info.txt and the backend log.
    pause
)
exit /b %GD_EXIT_CODE%
