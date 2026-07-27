# Geometry Dash ARM Wrapper — 2.2 beta Bringup20

Bringup20 focuses on the current v22 GDPS Editor/SubZero APK and restores the
cooperative HTTP wake-up behavior that made DynarmicTest14 networking reliable.
The temporary 90/95 MB APK is deliberately out of scope until a corrected build
is available.

## Changes in this branch

- Adds a cooperative `pthread_cond_wait` / `pthread_cond_signal` bridge for the
  newer Cocos2d-x HTTP worker. Bringup19 only woke semaphore-based workers, but
  this APK imports condition variables and no semaphore functions.
- Logs worker registration, wake triggers, condition waits, DNS, sockets,
  request send/receive, and final cooperative-network totals.
- Keeps the working desktop text-input fix, so the custom-song window no longer
  moves upward as though a mobile software keyboard appeared.
- Keeps saves local in `save-v22beta`; there is no drive-root `data/data` scan or
  migration.
- Removes the hard-coded APK byte-count and SHA-256 rejection. The builder uses
  the APK explicitly supplied on the command line and its own embedded native
  libraries only. No donor APK is loaded.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup20-dyn14-network\
```

Run `RUN_V22_SELECTED_APK.cmd`. Networking activity is written to
`gd-v22beta-selected.log`.
