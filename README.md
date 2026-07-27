# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup8

Separate ARMv7-A / Thumb-2 / VFPv3 / NEON bring-up branch. It does not replace the stable ARMv5 Test14-fix1 branch.

## Build with an external APK

No APK is included in this source package.

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-2.2-beta.apk"
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup8\RUN_V22_SELECTED_APK.cmd
```

## Bringup8 targets

### Official-level launch

The host low-level inflater already produced the correct megabyte-scale object data, but the beta passed an empty COW `std::string` into `PlayLayer::prepareCreateObjectsFromSetup`.

Bringup8 patches the single direct callsite to that parser. If its string argument is empty, the wrapper rebuilds it from the last verified `kS...` level payload using the beta's own guest string byte builder, then tail-calls the original parser with its original return address and ABI intact.

Expected markers:

```text
RESULT: DYNARMIC_V22_LEVEL_SETUP_BRIDGE_READY callsites=1 ...
[host] V22 PlayLayer setup repaired bytes=1241094 object=0x... data=0x... repair=1
```

### Wrench-and-hammer editor button

F2 has been removed as an editor workaround. It only opened My Levels and did not address the broken editor button.

Both beta engines export `EditLevelLayer::onEdit(CCObject*)` as a two-byte no-op. Bringup8 redirects the actual stored menu callback pointer to a host bridge that follows the companion `libgame.so` editor path:

- read the current `GJGameLevel` from `EditLevelLayer`;
- close active text input and verify the level name;
- create the real `LevelEditorLayer`;
- create a scene, add the editor layer, create a fade transition, and replace the current scene.

Expected markers:

```text
RESULT: DYNARMIC_V22_EDIT_BUTTON_BRIDGE_READY pointers=1 ...
[host] V22 wrench-and-hammer editor button editLayer=0x... level=0x...
RESULT: DYNARMIC_V22_LEVEL_EDITOR_ENTERED source=wrench-hammer count=1
```

## Retained fixes

- ARM exclusive monitor and `LDREX/STREX` support.
- FMOD 1.05.04-compatible host bridge and music pre-cache.
- 60 Hz Android refresh-rate response.
- Host APK member cache and text-input asset prewarm.
- Optional `libgame.so` detection without executing Android Dobby on Windows.
