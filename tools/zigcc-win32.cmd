@echo off
setlocal EnableExtensions EnableDelayedExpansion
if not defined ZIG (
  echo Set ZIG to the full path of zig.exe 1>&2
  exit /b 2
)
set "ZIGCC_ARGS="
:next_arg
if "%~1"=="" goto run_zig
if /I "%~1"=="-mthreads" goto skip_arg
if /I "%~1"=="-static-libgcc" goto skip_arg
set ZIGCC_ARGS=!ZIGCC_ARGS! "%~1"
:skip_arg
shift
goto next_arg
:run_zig
"%ZIG%" cc -target x86-windows-gnu !ZIGCC_ARGS!
exit /b !ERRORLEVEL!
