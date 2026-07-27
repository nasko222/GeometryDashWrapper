# Geometry Dash ARM Wrapper build notes

## NetworkTest3 v22 beta build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

The APK is copied to the output as `game-v22beta-selected.apk`. There is no
hard-coded APK size/hash allowlist and no donor-library argument.

Output:

```text
dist-arm-wrapper-v22beta-networktest3-wall-watchdog\
```

Run `RUN_NETWORKTEST3.cmd` from that output folder.
