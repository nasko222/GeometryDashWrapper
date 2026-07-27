# Geometry Dash ARM Wrapper — v22 beta NetworkTest3

`networktest3-wall-watchdog` is a separate branch for the current v22 beta APK. It is
focused only on keeping the client responsive while the emulated HTTP worker
runs.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-networktest3-wall-watchdog\
```

Run:

```text
RUN_NETWORKTEST3.cmd
```

The focused log is `gd-networktest3.log`.

## Scope

- Frame-sliced guest HTTP worker instead of recursive execution on the UI call.
- Nonblocking `connect`, `send`, and `recv` behavior for the newer curl client.
- One-millisecond worker-side `poll` cap per host frame.
- Existing desktop text-input offset suppression retained.
- Existing local `save-v22beta` storage retained.
- No temporary 90/95 MB APK size/hash check and no donor APK logic.
- Companion constructors/hooks are disabled in the NetworkTest3 launcher so network behavior is isolated.

## NetworkTest3 wall-clock watchdog

NetworkTest2 already moved DNS off the UI thread and made sockets nonblocking,
but the captured v22 worker could still remain inside one translated guest block
so long that `Jit::Run()` never returned to enforce the frame budget. NetworkTest3
preserves the full NetworkTest2 runtime and adds an asynchronous 4 ms hard halt to
the CCHttpClient worker only. The exact guest state is saved and resumed on the
next frame, preventing that guest/JIT stall from freezing the client.
