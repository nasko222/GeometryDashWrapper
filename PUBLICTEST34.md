# Geometry Dash Wrapper 0.9.6-publictest34

PublicTest34 targets the shared first-frame data/parser failure seen in Geometry Dash 2.11 and Geometry Dash SubZero after PublicTest33 successfully entered the real cocos2d `CCDirectorCaller` frame loop.

## Changes

- Implements Darwin/libc `atoi` / `atol` instead of returning zero for every parsed integer.
- Implements `strtol` and `strtoul` with guest end-pointer updates.
- Implements guest-memory `strtok` tokenization, including in-place delimiter NUL termination and continuation state.
- Implements a guest-safe `sscanf` subset for integer, unsigned/hex/octal, floating-point, string, character, scanset and `%n` conversions with ARMv7 variadic argument slots. This covers cocos2d BMFont parsing such as `padding=%d,%d,%d,%d`.
- Adds precise invalid-memory diagnostics: read vs write, access size, and a stack-key/window dump.
- Adds targeted `new(0x2c)` traces during frame execution for the CCString-style factory immediately preceding the shared objectDefinitions crash.
- Adds early completed-frame/present counters so a black host window can be distinguished from a frame callback that never returns.

## Why

PT33 reached the real frame loop and loaded/uploaded the major game sheets. Both GD 2.11 and SubZero then failed immediately after `objectDefinitions.plist` with an access at address `0x18`. The same logs also showed `_atoi`, `_sscanf` and `_strtok` still falling through incomplete bootstrap behavior. In particular, every `_atoi` result was zero, which is invalid for real game/config parsing.

PT34 fixes those parser primitives rather than bypassing the objectDefinitions code. If the `0x18` fault remains, the new `IOS FAULT ACCESS` and `IOS FAULT STACK KEY` records identify whether the failing instruction is the `CCString` object field write or the later conditional libc++ string-data read.

## Expected markers

During startup/data parsing:

```
IOS LIBC PARSE: atoi '...' -> ...
IOS LIBC PARSE: sscanf fmt='padding=%d,%d,%d,%d' ... assigned=4
IOS LIBC PARSE: strtok -> '...'
```

After a completed frame:

```
IOS HOSTGL PRESENT: completed-frame=1 presents=...
```

If the shared low-address fault remains:

```
IOS FAULT ACCESS: type=read|write size=4 address=0x18
IOS FAULT STACK KEY: ...
IOS CXX TRACE: new(0x2c) caller=0x... -> 0x...
```

## Scope

GD 1.0 and the older 1.81/1.90/1.91 builds still have separate pre-frame low-address faults. PublicTest34 prioritizes the newer 2.11/SubZero shared frame-time path first.
