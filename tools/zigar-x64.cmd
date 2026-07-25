@echo off
if not defined ZIG (
  echo Set ZIG to the full path of zig.exe 1>&2
  exit /b 2
)
"%ZIG%" ar %*
exit /b %ERRORLEVEL%
