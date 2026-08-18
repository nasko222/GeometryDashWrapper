# Geometry Dash Wrapper 0.9.6-publictest28

PublicTest28 targets the first common post-bootstrap crash seen in the iOS Geometry Dash builds after PublicTest27.

## What PT27 proved

PublicTest27's constructor, LC_MAIN/LC_UNIXTHREAD, dispatch_once and C++ string work is active. Geometry Dash 1.81, 1.90, 1.91 and 2.11 all reach the real AppController delegate and create the host OpenGL window. They then take the same path into the bundled Everyplay SDK and fail in `EveryplayCommon +isJailbroken`.

The exact PT27 fault is a null dereference immediately after Darwin `__error()`:

- Everyplay probes `/bin/bash` with `fopen`.
- The wrapper correctly reports that the path is absent.
- PT27 did not implement `__error`, so the generic import stub returned null.
- Guest code immediately dereferenced the returned `int *` to read errno.

In the 1.91 binary the faulting instruction is at 0x4234d2 (`ldr r5, [r0]`) immediately after the `___error` import call. Equivalent faults appear in 1.81, 1.90 and 2.11.

## PublicTest28 changes

### Obsolete Everyplay SDK is now unavailable by policy

Everyplay is an obsolete recording/telemetry/OAuth SDK and is not required for Geometry Dash gameplay. The Windows compatibility runtime now:

- no-ops `Everyplay +setClientId:clientSecret:redirectURI:...`
- reports `Everyplay +isSupported` as NO
- reports `EveryplayFeatures +isSupported` as NO

This keeps the game from spending startup inside an unsupported third-party telemetry stack before cocos2d can create its first scene. This mirrors the existing TestFlight telemetry bypass used by the iOS backend.

Expected log marker:

```
IOS: bypassing Everyplay +setClientId:clientSecret:redirectURI: ... policy=service-unavailable
```

### Darwin errno support

PT28 implements a real guest errno cell and `__error()` pointer semantics. Read-only virtual-filesystem failures now set meaningful Darwin/POSIX-style values:

- missing `fopen` path -> ENOENT (2)
- write/open mode rejected by the read-only asset VFS -> EACCES (13)
- `fclose(NULL)` -> EBADF (9)

Expected diagnostic when the path is exercised:

```
IOS LIBC: __error -> 0x... value=2
```

### ARC/runtime cleanup for later GD builds

PT27 still accidentally let several later Objective-C ARC functions fall through to the generic zero stub. PT28 handles:

- `objc_retainAutoreleaseReturnValue`
- `objc_retainBlock`
- `objc_storeStrong`
- weak-reference init/store/load/destroy helpers
- autorelease-pool push/pop

It also supplies lightweight bootstrap objects for `CFUUIDCreate`, `CFUUIDCreateString`, `CFRelease` and `dispatch_queue_create`.

### iostream startup noise

The old libstdc++ `std::ios_base::Init` constructor/destructor are now explicit no-ops rather than consuming the unknown-import log budget. Geometry Dash does not need host iostream state for the iOS game runtime.

## Test priority

1. Geometry Dash 1.91
2. Geometry Dash 1.90 / 1.81
3. Geometry Dash 2.11
4. Geometry Dash 1.0 separately if desired

The key success signal is that the log gets past the Everyplay registration point and continues into Geometry Dash/cocos2d manager and scene setup. If it still closes, the next `RESULT:` and `fault-pc=` line should now identify a new blocker after the telemetry SDK.
