# Geometry Dash ARM Wrapper — NetworkTest4

`networktest4-dyn14-wake-trace` keeps the complete v22 beta NetworkTest3
baseline and changes only the network-worker execution path plus diagnostics.

The key fix restores DynarmicTest14's immediate worker wake from `sem_post`.
NetworkTest2/3 deferred that wake to the next frame, which changed the ordering
of CCHttpClient's foreground/worker synchronization. NetworkTest4 also wakes
immediately on condition signals while retaining asynchronous DNS, nonblocking
sockets, frame slicing, and the hard worker watchdog.

The branch is intentionally verbose. The log now shows request creation,
`pthread_create`, semaphores/conditions, DNS, sockets, send/receive/poll, caller
addresses, a 512-call rolling import history, and 250 ms heartbeats during long
guest calls.

## Build

Place the selected APK beside the built executable as
`game-v22beta-selected.apk`, then run on the Windows build machine:

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Output:

```text
dist-arm-wrapper-v22beta-networktest4-dyn14-wake-trace\
```

Primary diagnostics:

```text
gd-networktest4.log
gd-networktest4-imports.txt
gd-networktest4-profile.csv
gd-networktest4-profile-summary.txt
```

The source archive intentionally excludes APK files.
