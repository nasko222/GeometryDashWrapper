# Geometry Dash Wrapper 0.9.6-gdpsfixes6

Compatibility expansion based on the stable gdpsfixes5 branch.

## Hybrid / unusual Android APK support

This branch adds a guarded legacy-ARM compatibility mode for APKs that package an
old Geometry Dash `libgame.so` under `lib/armeabi-v7a/` and optionally add newer
ARMv7 companion modules that depend on that game library.

The motivating test APK (`1.3 GDPS v3.0.0f.apk`) contains only ARM libraries:
`libgame.so`, `libascella.so`, `libfmod.so`, and `libfmodL.so`.  The old game core
is compatible with the legacy 1.x runtime, while `libascella.so` uses ARMv7 /
Thumb-2 / VFP/NEON-era code and links back to hundreds of symbols in `libgame.so`.

### Guard rails

- Existing x86 detection is unchanged and remains first priority.
- Existing `lib/armeabi/libgame.so` APKs still use the same legacy path.
- Existing 2.2-style `lib/armeabi-v7a/libcocos2dcpp.so` APKs still use ARMv7.
- A lone `lib/armeabi-v7a/libgame.so` now routes to legacy compatibility instead
  of being rejected.
- Auxiliary `.so` loading occurs only for libraries in the same v7 ABI directory
  that contain a dependency/reference to `libgame.so`.
- Normal gdpsfixes5 APKs never enter auxiliary-module loading.

### Hybrid execution

For a v7-packaged legacy game, the legacy Dynarmic CPU profile is raised from
ARMv5TE to ARMv7.  Dependent companion modules are mapped into a separate guest
address range, their relocations can resolve against the already loaded game/cocos
symbols, and Android/libc/OpenGL imports continue through the existing host bridge.
Hybrid-only `dlopen` / `dlsym` resolution exposes mapped game/companion symbols to
mods that resolve hooks dynamically.

The primary game's constructors and JNI_OnLoad still run first.  Companion
constructors and companion JNI_OnLoad are then run before nativeSetPaths/nativeInit.

## Regression boundary

The 1.0 color-picker fix, editor hotkeys, Extras menu, placeholder level ID 10,
Time Machine Beta ID 8, large-level upload fix, audio fixes, networking fixes, and
I_LOST_THE_GAME gate are carried forward unchanged from gdpsfixes5.
