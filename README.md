# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest11

DynarmicTest11 runs the ARMv5TE Android build of early Geometry Dash on 64-bit Windows through Dynarmic.

## Test11 changes

- Fixes the final libcurl connection rejection: Android `getsockopt(SO_ERROR)` now receives `0` after a successful Winsock connection instead of the incorrect guest `EIO` value `5`.
- Hooks `cocos2d::CCApplication::openURL` directly, so Facebook, Twitter, YouTube, artist, and RobTop links do not depend on the emulated JNI lookup path.
- Adds first-send, first-receive, and `SO_ERROR` diagnostics without logging passwords, level data, or HTTP bodies.
- Keeps Test10's completed nonblocking connect, host APK-member cache, Windows audio, mapped guest files, clean exit, editor text-input fix, and high-performance GPU preference.

## Build

Run:

```text
BUILD_DYNARMIC_X64.cmd
```

Output:

```text
dist-arm-wrapper-dynarmictest11
```

Use `RUN_DYNARMIC_INTERACTIVE.cmd` from that output folder. A custom GDPS APK can be passed to the PowerShell builder or copied as `game.apk` into the output folder after building.

## Important network log lines

```text
RESULT: DYNARMIC_WINSOCK_BRIDGE_READY
RESULT: DYNARMIC_CCAPPLICATION_OPENURL_HOOK_READY count=1
[host] Socket connect ... status=connected ...
[host] Socket SO_ERROR ... host=0 guest=0
[host] Socket first send ...
[host] Socket first recv ...
android log: response code: 200
```

For browser buttons, look for:

```text
[host] Browser open url=https://... result=ok
```
