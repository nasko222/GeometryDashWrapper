@echo off
setlocal
cd /d "%~dp0"

set "GDPS_SERVER=naskogdps17.7m.pl/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "FORCE_HIGHEST_GRAPHICS=true"
set "MUSIC_PULSE_MAX=0.30"
set "DISABLE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_DURING_PLAY=true"
set "VERSION_ISOLATED_SAVES=true"
set "V22_EXACT_EDITOR_VISIBILITY=false"

if not exist "GeometryDashLauncher.exe" (
  echo GeometryDashLauncher.exe is missing. Build the wrapper with BUILD_ALL.cmd.
  pause
  exit /b 2
)

if "%~1"=="" (
  "GeometryDashLauncher.exe"
) else (
  "GeometryDashLauncher.exe" "%~1"
)
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" pause
exit /b %RESULT%
