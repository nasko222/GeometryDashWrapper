# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest12

DynarmicTest12 runs the ARMv5TE Android build of early Geometry Dash on 64-bit Windows through Dynarmic.

## Test12 changes

- Replaces the Winsock `WSAPoll` translation with a POSIX-style `poll()` bridge implemented using Windows `select()`.
- When the old ARM libcurl calls `recv()` before the first response byte arrives, waits for socket readability and retries instead of immediately returning `WSAEWOULDBLOCK` into its fragile receive path.
- Applies the same bounded writable wait to `send()` if a nonblocking socket temporarily fills.
- Adds bounded diagnostics for requested/ready poll events, receive waits, EOF, guest-buffer rejection, and real Winsock errors.
- Keeps Test11's correct `SO_ERROR` mapping and direct `CCApplication::openURL` hook. Browser buttons were confirmed working in the Test11 runtime log.
- Keeps host APK-member caching, Windows audio, mapped guest files, clean exit, editor text-input handling, and high-performance GPU preference.

## Build

Run:

```text
BUILD_DYNARMIC_X64.cmd
```

Output:

```text
dist-arm-wrapper-dynarmictest12
```

Use `RUN_DYNARMIC_INTERACTIVE.cmd` from that folder. A custom GDPS APK can be passed to the PowerShell builder or copied over the output `game.apk` after building.

## Important network log lines

```text
RESULT: DYNARMIC_WINSOCK_BRIDGE_READY
RESULT: DYNARMIC_CCAPPLICATION_OPENURL_HOOK_READY count=1
[host] Socket connect ... status=connected ...
[host] Socket SO_ERROR ... host=0 guest=0
[host] Socket first send ...
[host] Socket poll ...
[host] Socket recv wait ... result=...
[host] Socket first recv ...
android log: response code: 200
```
