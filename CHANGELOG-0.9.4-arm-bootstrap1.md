# 0.9.4-arm-bootstrap1

This release turns the successful ARM-only compatibility probe into the first
graphical Windows bootstrap for Geometry Dash 1.0 through 1.4/1.41.

## Added

- Win32 OpenGL window, message loop, frame pacing, and lifecycle handling.
- Authentic guest `nativeInit` and `nativeRender` invocation.
- ARM JNI `JavaVM` and `JNIEnv` trap tables, including normal, `V`, and `A`
  call variants.
- `RegisterNatives` capture and registered/exported Cocos callback discovery.
- Mouse, Space/Up, Back/Escape, character, and Backspace delivery.
- Guest JNI strings, classes, methods, arrays, preferences, save files,
  language, identity, network state, URLs, keyboard, and audio services.
- APK-backed Android asset manager and guest stdio/POSIX file bridge.
- ARM OpenGL ES to Win32 OpenGL dispatch for early Cocos rendering calls.
- Existing Windows storage and MCI/Ogg audio integration.
- `build_arm_wrapper.py` and graphical/probe/relocation launchers.

## Preserved

- ARM ELF extraction and mapping.
- `R_ARM_*` relocation support.
- ARMv5/Thumb execution through Unicorn.
- Android kuser atomics/TLS support.
- Authentic ELF constructor execution.
- Authentic `JNI_OnLoad` execution and JNI 1.4 validation.
- Standalone `arm_probe.c` and `build_arm_probe.py` for regression testing.

## Current status

The source is C syntax-checked and the earlier native probe is proven by the
supplied logs. This environment did not contain a target APK or the Win32 Zig /
Unicorn build output, so the new graphical path has not been runtime-tested here.
The first real APK test may reveal additional imports, JNI methods, or OpenGL
edge cases; send `gd-arm-wrapper.log` from that run.
