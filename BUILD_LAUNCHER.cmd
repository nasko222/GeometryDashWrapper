@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_launcher.ps1" %*
exit /b %ERRORLEVEL%
