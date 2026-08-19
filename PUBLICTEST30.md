# Geometry Dash Wrapper 0.9.6-publictest30

PublicTest30 targets the common PT29 black-screen crash in Geometry Dash iOS 1.81/1.90/1.91/2.11/SubZero.

## Changes

- `dlsym` now returns executable synthetic import trampolines for non-empty system symbol names instead of NULL.
- Dynamically resolved AudioUnit, AudioSession, AUGraph and AudioQueue functions have safe bootstrap behavior.
- `alcGetProcAddress` / `alGetProcAddress` now resolve requested OpenAL extensions to executable synthetic trampolines.
- `AudioQueueNewOutput` writes a stable guest queue handle through its output parameter.
- Keeps the PT29 Everyplay quarantine, Darwin errno fixes, Mach-O constructor support, LC_MAIN support and C/C++ runtime bridge.

## Why

PT29 logs showed the Objective-C Everyplay quarantine firing, but the crash still occurred at a direct `BLX` through a function pointer. Earlier startup logs showed the corresponding system functions were obtained with `dlsym` and PT29 returned NULL. Several GD/Everyplay/CocosDenshion paths later call those cached pointers directly. PublicTest30 fixes the dynamic-symbol boundary rather than adding another Objective-C selector bypass.

## Test focus

Start with Geometry Dash 1.91, then 1.90/2.11/SubZero, then 1.81. Look for `IOS DYNSYM: dlsym ... -> synthetic stub` and whether the old `address=0x3ffc pc=0x4000/0x4004` crash disappears.

Geometry Dash 1.0 still has a separate imported C++ RTTI/vtable-data issue and is not claimed fixed by this test.
