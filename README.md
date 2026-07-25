# Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest2

`overkilltest2` is a destructive diagnostic branch based on `performancetest1`.
It is not intended to look correct. Its purpose is to remove complete subsystems
one layer at a time and reveal where the remaining ARM slowdown actually lives.

The stable save transactions, immutable-memory protection, allocator fixes,
label compatibility and indexed APK lookup remain enabled.

## Build on Windows

Double-click:

```text
BUILD_WINDOWS.cmd
```

The portable builder downloads Zig, CMake and Ninja into `.build-tools` and
creates:

```text
dist-arm-wrapper-overkilltest2\
```

No Visual Studio, WSL, Linux, MSYS2 or administrator rights are required.

## Recommended test

Run `RUN_ARM_NATIVE_BOOT.cmd`. This starts a navigable visual mode with:

- the audio subsystem never initialized;
- particle, trail and cosmetic ARM functions patched to return immediately;
- every original PNG replaced before decoding by a 70-byte 1x1 white PNG;
- every texture upload replaced with a 1x1 white texture.

Enter the same heavy Clutterfunk section, then toggle one layer at a time:

```text
F3  nativeRender itself
F4  ARM sprite/label/shape draw methods
F5  particles and cosmetic effects
F6  actual host OpenGL draw calls
F7  CCNode scene traversal
F8  the complete host OpenGL backend
F9  all future texture uploads
F10 write the current state to the log
F11 capture a five-second ARM hot-block profile

Function-key auto-repeat is ignored, so one press changes each diagnostic state exactly once.
```

Leave each test enabled for at least one complete five-second profile window,
then toggle it back before testing the next layer.

After reproducing the slow section with normal rendering, press **F11 once**.
It automatically profiles translated ARM basic blocks for five seconds, restores
normal-speed execution, and writes `ARM hot block profile` with nearest function
names to `gd-arm-wrapper.log`.

See `OVERKILLTEST2-NOTES.md` for the interpretation table and stronger launchers.
