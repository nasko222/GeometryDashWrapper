@echo off
setlocal
cd /d "%~dp0"

set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=FALSE"
set "FULL_BYPASS=TRUE"
set "FORCE_HIGHEST_GRAPHICS=TRUE"
set "MUSIC_PULSE_MAX=0.30"
set "FPS=VSYNC"
set "RESOLUTION=1140x640"
set "TEXTURE_FILTERING=GAME"
set "ANTIALIASING=NONE"
set "SHOW_COMMAND_PROMPT=TRUE"
set "OLD_VER_PLAYTEST=TRUE"
set "RESTART_BUTTON=TRUE"
set "VERSION_ISOLATED_SAVES=TRUE"
set "I_LOST_THE_GAME=TRUE"
set "EDITOR_CONTROLLS=TRUE"

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
