@echo off
setlocal
set "TARGET=%~1"
if not defined TARGET set "TARGET=..\GeometryDashWrapper-0.9.5-endurancetest4"
if not exist "%TARGET%\VERSION.txt" (
  echo Target source folder not found: %TARGET%
  echo Drag the EnduranceTest4 source folder onto this script.
  exit /b 1
)
for /r "%~dp0" %%F in (*) do (
  set "FILE=%%F"
  call :copy_one "%%F"
)
del /q "%TARGET%\ENDURANCETEST4.md" 2>nul
del /q "%TARGET%\ENDURANCETEST4-VERIFICATION.txt" 2>nul
echo EnduranceTest5 hotfix applied to %TARGET%
exit /b 0

:copy_one
set "SOURCE=%~1"
set "REL=%SOURCE:%~dp0=%"
if /i "%REL%"=="APPLY_TO_ENDURANCETEST4.cmd" exit /b 0
if /i "%REL%"=="HOTFIX-README.txt" exit /b 0
for %%D in ("%TARGET%\%REL%") do if not exist "%%~dpD" mkdir "%%~dpD"
copy /y "%SOURCE%" "%TARGET%\%REL%" >nul
exit /b 0
