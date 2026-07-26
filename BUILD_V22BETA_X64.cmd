@echo off
setlocal
cd /d "%~dp0"
call "%~dp0BUILD_DYNARMIC_X64.cmd" %*
exit /b %ERRORLEVEL%
