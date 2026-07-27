# Geometry Dash ARM Wrapper — NetworkTest5

`networktest5-dyn14-exact-network` keeps the complete NetworkTest4/v22 beta
runtime baseline and changes only `src/dynarmic_probe.cpp` in the runtime.

## Why NetworkTest4 froze

Both supplied traces register the CCHttpClient worker, execute two short worker
slices, then stop forever as slice 3 resumes OpenSSL around `CRYPTO_zalloc`.
No DNS resolver, socket, connect, send, or receive import is reached. The hard
watchdog only surrounded `cpu_.Run()`; it could not make the sliced OpenSSL
execution model equivalent to the known-working DynarmicTest14 worker.

## NetworkTest5 behavior

NetworkTest5 removes worker frame slicing completely. A request signal resumes
the ARM CCHttpClient worker immediately and runs it continuously until the guest
reaches `pthread_cond_wait`, `sem_wait`, or exits. This is the DynarmicTest14
run-to-wait model, adapted to this beta's condition-variable worker.

It also restores DynarmicTest14's network semantics:

- synchronous DNS in the HTTP worker;
- host-completed nonblocking `connect` with the original 15-second bound;
- original poll timeout behavior;
- no render-frame worker pumping;
- no pause/resume inside OpenSSL request setup.

The NetworkTest4 forensic import ring and request/thread/socket logging remain.
All editor, save, audio, platformer, lifecycle, APK-cache, inflate, and companion
features outside this one translation unit are unchanged.

## Build

On the Windows build machine:

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Output:

```text
dist-arm-wrapper-v22beta-networktest5-dyn14-exact-network\
```

Diagnostics:

```text
gd-networktest5.log
gd-networktest5-imports.txt
gd-networktest5-profile.csv
gd-networktest5-profile-summary.txt
```

The source archive intentionally excludes APK files.
