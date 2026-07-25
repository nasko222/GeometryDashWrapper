# Building Geometry Dash ARM Wrapper overkilltest2

## Windows one-click build

Run:

```text
BUILD_WINDOWS.cmd
```

or from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The builder uses portable Zig 0.14.1, CMake 3.31.10 and Ninja 1.13.2. It builds
the bundled ARM-only Unicorn 2.1.4 static archive and then the 32-bit Windows
wrapper. Nothing is installed system-wide.

Output:

```text
dist-arm-wrapper-overkilltest2\
```

The bundled `game.apk` is copied automatically. To use another APK:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1 -Apk "D:\path\game.apk"
```

Use `RUN_ARM_NATIVE_BOOT.cmd` first. See `OVERKILLTEST2-NOTES.md` before using
the blank-screen diagnostic launchers.
