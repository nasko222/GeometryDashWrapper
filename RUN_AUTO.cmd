@echo off
cd /d "%~dp0"
python run_auto.py %*
exit /b %ERRORLEVEL%
