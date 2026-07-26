# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest10

DynarmicTest10 runs the ARMv5TE Android build of early Geometry Dash on 64-bit Windows through Dynarmic.

## Test10 changes

- Fixes GDPS/HTTP connections that Test9 incorrectly rejected while Winsock was still completing a nonblocking TCP connection.
- Opens Facebook, Twitter, artist, and other in-game HTTP/HTTPS links in the default Windows browser.
- Keeps the host-level APK member cache, persistent asset cache, Windows audio, mapped guest files, clean exit, editor text input fix, and high-performance GPU preference.

## Build

Run:

```text
BUILD_DYNARMIC_X64.cmd
```

Output:

```text
dist-arm-wrapper-dynarmictest10
```

Use `RUN_DYNARMIC_INTERACTIVE.cmd` from that output folder. A custom GDPS APK can be passed to the PowerShell builder or copied as `game.apk` into the output folder after building.

## Important network log lines

```text
RESULT: DYNARMIC_WINSOCK_BRIDGE_READY
[host] DNS getaddrinfo ...
[host] Socket connect ... status=connected ...
android log: response code: ...
```

For browser buttons, look for:

```text
JNI method: org/cocos2dx/lib/Cocos2dxActivity.openURL (Ljava/lang/String;)V
[host] Browser open url=... result=ok
```
