# V22 beta Bringup18 — runtime companion hooks

Bringup18 is based on Bringup17 and addresses the exact failures from its test log.

## End-screen crash

The crash occurred in `cocos2d::CCString::floatValue()` with `r0 == 0` and a
null-underflow memory address. The wrapper now recognizes only that narrow
fault shape while executing `nativeRender`, resumes as if `CCString::length()`
returned zero, and lets `floatValue()` use its normal empty-string result.
Other invalid guest accesses still stop the runtime normally.

## Companion features

Bringup17 mapped `libgame.so` only for `LevelEditorLayerExt::initH` and explicitly
left its constructors and `ApplyHooks` routines disabled. Bringup18 can now:

1. run the companion's ARM constructors;
2. execute named `ApplyHooks` feature groups;
3. implement `HookManager::do_hook` inside the wrapper, without executing
   `libhooking.so` or Dobby;
4. rewrite primary-library function references and clear Dynarmic's code cache.

`--companion-hooks=safe` is the default launcher profile. It enables editor/menu,
options, collision/shader fixes, timer, search, icon, label, emoji, and level-info
hooks. `RUN_V22_SELECTED_APK_ALL_HOOKS.cmd` adds DPAD, GDPS manager, servers,
hacks, and developer hooks. The all profile is intentionally marked experimental.

## Editor support across APKs

- The selected 144 MB beta contains its own compatible `libgame.so`; no donor is
  needed.
- Stock SubZero uses the same late primary symbol layout but has no editor
  companion. Build with the selected beta as a second argument so its
  `libgame.so` is copied as a sidecar:

  ```bat
  BUILD_V22BETA_X64.cmd "D:\APKs\SubZero.apk" "D:\APKs\selected-beta.apk"
  ```

- The 95 MB early beta has a different primary ABI/layout. Its `libgdkit.so` is
  a hook/toolkit library, not the missing full editor implementation. Bringup18
  refuses to inject the late-beta donor there instead of corrupting it.

## Build and run

```bat
BUILD_V22BETA_X64.cmd "D:\APKs\selected-beta.apk"
```

Run `RUN_V22_SELECTED_APK.cmd` first. Use
`RUN_V22_SELECTED_APK_ALL_HOOKS.cmd` when testing every packaged feature group.

No APK or extracted proprietary `.so` is included in this source archive.
