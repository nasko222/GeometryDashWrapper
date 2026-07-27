# Geometry Dash ARM Wrapper build notes

## Bringup20 v22 beta build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\current-v22-beta.apk"
```

The APK is copied to the output as `game-v22beta-selected.apk`. There is no
hard-coded APK size/hash allowlist and no donor-library argument.

Output:

```text
dist-arm-wrapper-v22beta-bringup20-dyn14-network\
```
