# V22 Beta NetworkTest

This is a separate networking-only test branch for the current 140 MB v22 beta APK.

## Scope

- Keeps the working desktop text-input offset suppression.
- Keeps local `save-v22beta` storage.
- Does not contain the temporary incorrect 90/95 MB APK check or donor logic.
- Does not attempt to fix the editor or companion feature hooks in this branch.

## Why Bringup20 froze

Bringup20 resumed the emulated HTTP worker recursively from `pthread_create` and
`pthread_cond_signal`, then allowed that worker to run synchronously for up to
120 seconds. Its socket compatibility path could also wait up to 15 seconds in
`connect`, `send`, or `recv`. All of that happened on the Win32/UI thread.

## NetworkTest scheduler

- `pthread_create` only registers the guest worker.
- Signals only mark it runnable; they never execute it recursively.
- The main host loop runs the worker in bounded 3 ms / 24-run slices.
- A condition or semaphore wait yields the worker until another signal arrives.
- Network logs flush immediately so a forced close still leaves the last state.

## NetworkTest sockets

- New sockets are nonblocking from creation.
- `connect` returns guest `EINPROGRESS` instead of waiting on the UI thread.
- `send` and `recv` return `EAGAIN` on `WSAEWOULDBLOCK`.
- Worker-side `poll` is limited to 1 ms per host frame.
- Curl is expected to finish the operation through `poll` and `SO_ERROR`.

Build with:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

Run:

```text
RUN_NETWORKTEST.cmd
```

The most useful log lines begin with `NetworkTest`, `DNS`, or `Socket`.
