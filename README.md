# Geometry Dash ARM Wrapper — v22 beta networktest

`networktest` is a separate branch for the current 140 MB v22 beta APK. It is
focused only on keeping the client responsive while the emulated HTTP worker
runs.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-networktest\
```

Run:

```text
RUN_NETWORKTEST.cmd
```

The focused log is `gd-networktest.log`.

## Scope

- Frame-sliced guest HTTP worker instead of recursive execution on the UI call.
- Nonblocking `connect`, `send`, and `recv` behavior for the newer curl client.
- One-millisecond worker-side `poll` cap per host frame.
- Existing desktop text-input offset suppression retained.
- Existing local `save-v22beta` storage retained.
- No temporary 90/95 MB APK size/hash check and no donor APK logic.
- Editor and companion feature hooks are not part of this branch.
