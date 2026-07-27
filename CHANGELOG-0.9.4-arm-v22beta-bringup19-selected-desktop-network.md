# 0.9.4-arm-v22beta-bringup19-selected-desktop-network

- Narrows the release/build workflow to the selected 144,490,721-byte APK.
- Removes legacy drive-root Android save migration and recovery launchers.
- Keeps all saves directly under `save-v22beta`.
- Disables Android software-keyboard scene panning on desktop text fields.
- Implements missing `gethostbyname`, `getnameinfo`, `shutdown`, `writev`,
  `pipe`, and `socketpair` networking/runtime calls.
- Preserves Bringup18 selected-APK gameplay fixes and safe companion features.
