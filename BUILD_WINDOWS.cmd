@echo off
setlocal
cd /d "%~dp0"
echo Geometry Dash ARM Wrapper overkilltest2 Windows build
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" (
  echo BUILD FAILED with error %RESULT%.
) else (
  echo BUILD FINISHED.
)
pause
exit /b %RESULT%
