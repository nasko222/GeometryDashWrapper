@echo off
setlocal
cd /d "%~dp0"
call BUILD_X86.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
call BUILD_DYNARMIC.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist dist-unified\save mkdir dist-unified\save
copy /y run_auto.py dist-unified\run_auto.py >nul
copy /y GeometryDash.cfg dist-unified\GeometryDash.cfg >nul
xcopy /e /i /y assets\icons dist-unified\assets\icons >nul
echo.
echo GeometryDash.exe and all three private backends are in dist-unified\
exit /b 0
