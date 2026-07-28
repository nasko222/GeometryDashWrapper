# Building 0.9.5-unified2-fix2

Run on 64-bit Windows:

```bat
BUILD_ALL.cmd
```

The build creates:

```text
dist-unified/
  x86/
  arm-legacy/
  armv7/
  RUN_AUTO.cmd
  run_auto.py
```

Put `game.apk` in `dist-unified/`, edit the four settings at the top of
`RUN_AUTO.cmd`, and run it.

The x86 build uses portable Zig 0.14.1. Both ARM builds use the same Dynarmic
6.7.0 checkout through portable Zig, CMake and Ninja. No Unicorn source or
runtime is used.

Fix2 adds WinHTTP to the shared source list because x86 and legacy ARM route
`getGJSongInfo.php` through official HTTPS Boomlings before returning the raw
HTTP response to the guest networking library.
