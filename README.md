# Geometry Dash ARM Wrapper — v22 beta NetworkTest2

`networktest2-async-dns` is a separate branch for the current v22 beta APK. It is
focused only on keeping the client responsive while the emulated HTTP worker
runs.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-networktest2-async-dns\
```

Run:

```text
RUN_NETWORKTEST2.cmd
```

The focused log is `gd-networktest2.log`.

## Scope

- Frame-sliced guest HTTP worker instead of recursive execution on the UI call.
- Nonblocking `connect`, `send`, and `recv` behavior for the newer curl client.
- One-millisecond worker-side `poll` cap per host frame.
- Existing desktop text-input offset suppression retained.
- Existing local `save-v22beta` storage retained.
- No temporary 90/95 MB APK size/hash check and no donor APK logic.
- Companion constructors/hooks are disabled in the NetworkTest2 launcher so network behavior is isolated.

## NetworkTest2 async DNS

The original NetworkTest still froze because the guest timeslice did not bound time spent inside a synchronous host import. NetworkTest2 moves DNS resolution off the UI thread and resumes the parked guest worker only after the native resolver completes or times out.
