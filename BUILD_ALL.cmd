@echo off
setlocal
cd /d "%~dp0"
call BUILD_X86.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
call BUILD_DYNARMIC.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
copy /y RUN_AUTO.cmd dist-unified\RUN_AUTO.cmd >nul
copy /y run_auto.py dist-unified\run_auto.py >nul
echo.
echo All three backends are in dist-unified\
exit /b 0
