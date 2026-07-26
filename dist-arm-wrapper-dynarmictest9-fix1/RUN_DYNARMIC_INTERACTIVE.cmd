@echo off
setlocal
cd /d "%~dp0"
GeometryDashDynarmicProbe.exe game.apk --log=gd-dynarmic-interactive.log
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Dynarmic interactive wrapper failed. See gd-dynarmic-interactive.log.
if "%RESULT%"=="0" echo Dynarmic interactive wrapper closed cleanly. See gd-dynarmic-interactive.log.
pause
exit /b %RESULT%