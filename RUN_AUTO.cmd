@echo off
cd /d "%~dp0"

rem ================================================================
rem Geometry Dash Wrapper 0.9.5-unified2 launch settings
rem Edit only the values on the right side.
rem Booleans accept true or false.
rem ================================================================

rem API base. You may include http:// or https:// and a custom port.
rem Examples:
rem   www.boomlings.com/database
rem   http://game.example.com/server
set "GDPS_SERVER=www.boomlings.com/database"

rem Make every exported icon/color unlock check return true for this run.
rem This does not permanently write unlocks into the save file.
set "HACK_ICONS=false"

rem In spin-offs, redirect the full-version creator button to My Levels.
set "FULL_BYPASS=true"

rem Prefer an available ARM backend even when the APK also contains x86.
rem false keeps the normal x86-first priority.
set "OVERRIDE_ARM=false"

python run_auto.py %*
exit /b %ERRORLEVEL%
