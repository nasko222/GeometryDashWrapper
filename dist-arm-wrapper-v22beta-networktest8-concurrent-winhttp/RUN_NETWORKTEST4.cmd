@echo off
setlocal
cd /d "%~dp0"
if not exist game-v22beta-selected.apk (
  echo game-v22beta-selected.apk is missing.
  echo Rebuild with: BUILD_V22BETA_X64.cmd "D:\path\to\beta.apk"
  pause
  exit /b 2
)
GeometryDashDynarmicProbe.exe game-v22beta-selected.apk --companion-hooks=off --debug-everything --dump-imports=gd-networktest8-imports.txt --log=gd-networktest8.log --profile=gd-networktest8-profile.csv --profile-summary=gd-networktest8-profile-summary.txt
set "RESULT=%ERRORLEVEL%"
echo.
echo Main log: gd-networktest8.log
echo Imports: gd-networktest8-imports.txt
pause
exit /b %RESULT%