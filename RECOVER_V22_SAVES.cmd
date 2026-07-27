@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0RECOVER_V22_SAVES.ps1" %*
echo.
pause
exit /b %ERRORLEVEL%
