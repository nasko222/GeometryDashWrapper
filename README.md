# Geometry Dash ARM Wrapper — 2.2 beta Bringup19

Bringup19 is intentionally focused on the selected **144,490,721-byte** GDPS
Editor/SubZero beta APK. It keeps the stable gameplay/platformer and companion
feature work from Bringup18, but stops trying to make unrelated APKs share the
same editor ABI.

## Changes in this branch

- Saves live only in `save-v22beta`; no mounted-drive `data/data` recovery scan.
- Android software-keyboard panning is disabled on Windows, including the
  custom-song ID field.
- The POSIX/WinSock bridge now implements `gethostbyname`, `getnameinfo`,
  `shutdown`, `writev`, `pipe`, and `socketpair` instead of returning empty
  stubs.
- The selected APK continues to use its own matching `libgame.so`; no donor
  library is accepted by the build script.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\game-v22beta-selected.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup19-selected-desktop-network\
```

Run `RUN_V22_SELECTED_APK.cmd`. The optional all-hooks launcher remains for
feature comparison, but the safe launcher is the normal test target.

All persistent files are under `save-v22beta`. Guest `/data/data/...` strings
are intercepted in memory and mapped there; the wrapper never scans a real
`D:\data\data` tree.
