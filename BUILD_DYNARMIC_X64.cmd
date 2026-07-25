@echo off
setlocal
cd /d "%~dp0"
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-dynarmic-x64.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Dynarmic x64 build failed with exit code %RESULT%.
if "%RESULT%"=="0" echo Dynarmic x64 bring-up probe built successfully.
exit /b %RESULT%
