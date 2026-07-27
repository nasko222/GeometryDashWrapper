@echo off
setlocal
cd /d "%~dp0"
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_x86.ps1" %*
exit /b %ERRORLEVEL%
