@echo off
setlocal
cd /d "%~dp0"

set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "FORCE_HIGHEST_GRAPHICS=true"
set "MUSIC_PULSE_MAX=0.30"
set "VERSION_ISOLATED_SAVES=true"
set "I_LOST_THE_GAME=true"
set "EDITOR_CONTROLLS=true"
set "EXTRAS_MENU=true"

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
