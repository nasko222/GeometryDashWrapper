# Building Geometry Dash ARM Wrapper 0.9.4-arm-v22beta-bringup9

From a fresh extracted folder, run:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup9\
```

Launch:

```bat
dist-arm-wrapper-v22beta-bringup9\RUN_V22_SELECTED_APK.cmd
```

The selected APK must remain external to the source archive. A newer beta containing `lib/armeabi-v7a/libgame.so` is required for the companion editor initializer. Earlier APKs without that module still receive the level-settings fallback but cannot use the full companion editor initialization.

Test normal gameplay first, then open My Levels and press the wrench-and-hammer button on a level entry. F2 is not intercepted.
