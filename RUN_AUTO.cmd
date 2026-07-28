@echo off
cd /d "%~dp0"

rem ================================================================
rem Geometry Dash Wrapper 0.9.5-unified7-recovery launch settings
rem Edit only the values on the right side.
rem Booleans accept true or false.
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
rem true makes PlatformToolbox::isHD return true and low-memory return false.
set "FORCE_HIGHEST_GRAPHICS=true"

rem Cap the music-reactive visual meter (0.00 to 1.00).
rem Lower values reduce oversized rave/pulse effects without lowering audio.
set "MUSIC_PULSE_MAX=0.30"


python run_auto.py %*
exit /b %ERRORLEVEL%
