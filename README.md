# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup9

Bringup9 continues the separate ARMv7-A/Thumb-2/VFPv3/NEON Geometry Dash 2.2 beta branch. It does not modify the stable ARMv5 test14-fix1 branch and it does not merge x86/ARM/ARMv7 selection yet.

## What Bringup9 changes

### Selective level-settings recovery

Bringup8 proved that full level payload inflation works and that some levels, including Power Trip, already enter gameplay. A different level can still fail when the beta's own `LevelSettingsObject::objectFromString` rejects the first settings segment and returns null.

Bringup9 hooks only that one parser call inside `PlayLayer::prepareCreateObjectsFromSetup`:

1. Run the original parser with the original guest `std::string`.
2. If it succeeds, change nothing.
3. If it returns null, retry after removing only the beta-only `kS38` color-channel block.
4. If that is still rejected, retry with minimal default level settings.

The object list and gameplay payload are never replaced. A fallback may temporarily lose custom level colors, but it prevents a rejected settings header from crashing the entire level.

### Real companion editor initialization

The wrench-and-hammer bridge from Bringup8 successfully creates the editor scene and starts the fade transition. The crash in `LevelEditorLayer::draw` happens because the beta's main `LevelEditorLayer::init` is a disabled four-byte stub; important members such as the array at `+0x2BFC` remain null.

When the selected APK contains `lib/armeabi-v7a/libgame.so`, Bringup9 maps and relocates that companion ARM module beside `libcocos2dcpp.so`. Undefined Cocos/game symbols resolve against the already loaded main library, while normal Android/libc/FM0D/OpenGL imports reuse the wrapper's host stubs.

The wrapper then calls the companion's real:

```text
LevelEditorLayerExt::initH(GJGameLevel*)
```

on the editor object before attaching it to the new scene. The companion's Android Dobby/JNI hook installer is not executed; only the targeted editor initializer is used.

## Build

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\beta.apk"
```

Run:

```bat
dist-arm-wrapper-v22beta-bringup9\RUN_V22_SELECTED_APK.cmd
```

Use a fresh extracted source folder. The APK remains external and is copied only to the local generated distribution directory.

## Test order

1. Start Power Trip or another level that already worked in Bringup8.
2. Start Knock Em Out or the level that previously crashed after successful inflation.
3. Open My Levels, select a level, and press the wrench-and-hammer button.
4. If the editor opens, place an object, playtest, pause, and exit back to My Levels.

## Expected markers

```text
RESULT: DYNARMIC_V22_LEVEL_SETTINGS_FALLBACK_READY callsites=1 mode=native-first+strip-kS38+minimal-default
RESULT: DYNARMIC_V22_COMPANION_EDITOR_RUNTIME_READY image=0x18000000-0x180a3000 initH=0x... constructors=not-run targeted-init=1
```

Only a rejected level settings header should produce:

```text
WARNING: V22 level settings parser fallback mode=strip-kS38 ...
```

Editor initialization should produce:

```text
RESULT: DYNARMIC_V22_COMPANION_EDITOR_INIT_OK editor=0x... level=0x... init=0x...
RESULT: DYNARMIC_V22_LEVEL_EDITOR_ENTERED source=wrench-hammer count=1
```

## Limits

The Windows executable cannot be runtime-tested in this Linux packaging environment. Static loading, relocation, hook discovery, companion symbol resolution, and C++20 syntax validation are performed here; the Windows log remains the authoritative gameplay test.
