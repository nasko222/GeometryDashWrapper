@echo off
setlocal
cd /d "%~dp0"
GeometryDashDynarmicProbe.exe game.apk --probe-only --log=gd-dynarmic-probe-only.log
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%