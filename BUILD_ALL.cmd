@echo off
setlocal
cd /d "%~dp0"

call BUILD_X86.cmd
if errorlevel 1 exit /b %ERRORLEVEL%

call BUILD_DYNARMIC.cmd
if errorlevel 1 exit /b %ERRORLEVEL%

call BUILD_LAUNCHER.cmd
if errorlevel 1 exit /b %ERRORLEVEL%

if not exist dist-unified\save mkdir dist-unified\save
copy /y RUN_AUTO_GDPS.cmd dist-unified\RUN_AUTO_GDPS.cmd >nul
copy /y RUN_AUTO_BOOMLINGS.cmd dist-unified\RUN_AUTO_BOOMLINGS.cmd >nul
if exist dist-unified\RUN_AUTO.cmd del /q dist-unified\RUN_AUTO.cmd
if exist dist-unified\run_auto.py del /q dist-unified\run_auto.py
if exist dist-unified\assets rmdir /s /q dist-unified\assets
xcopy /e /i /y assets dist-unified\assets >nul

echo.
echo All four backends and the native launcher are in dist-unified\
echo Drag an APK onto RUN_AUTO_GDPS.cmd or RUN_AUTO_BOOMLINGS.cmd to run it.
echo Drag an ARMv7 decrypted IPA onto either launcher to analyze it and attempt the iOS bootstrap.
exit /b 0
