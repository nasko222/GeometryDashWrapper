# Geometry Dash Wrapper 0.9.5-unified2

Unified2 keeps the three exact last known-good execution lines without Unicorn:

- **x86 native, default highest priority:** `0.9.3-alpha3`
- **legacy ARM/Thumb through Dynarmic:** `0.9.4-arm-dynarmictest14-fix1`
- **ARMv7 / Geometry Dash 2.2 through Dynarmic:** `0.9.4-milestone1`

The package contains no APK, extracted proprietary game library, executable, or
Unicorn dependency.

## Editable launch settings

Build the project, open `dist-unified/RUN_AUTO.cmd`, and edit the four values at
the top:

```bat
set "GDPS_SERVER=www.boomlings.com/database"
set "HACK_ICONS=false"
set "FULL_BYPASS=true"
set "OVERRIDE_ARM=false"
```

- `GDPS_SERVER` changes the recognized Geometry Dash PHP API base while
  preserving endpoint names such as `getGJLevels21.php`. It accepts
  `host/path`, `http://host/path`, or `https://host/path`.
- `HACK_ICONS=true` makes exported icon and color unlock checks return true for
  that run. It does not permanently write fake unlocks into the save.
- `FULL_BYPASS=true` preserves Milestone1 behavior: spin-off full-version
  creator buttons redirect to My Levels, and compatible creator gates report
  available. Set it to `false` for authentic behavior.
- `OVERRIDE_ARM=true` chooses a supported ARM backend when the same APK also
  includes x86. With `false`, x86 remains the first choice.

ARMv7 uses the setting directly in the shared WinHTTP bridge. The x86 and
legacy-ARM backends rewrite recognized plaintext Geometry Dash API requests and
DNS targets while retaining their original bundled networking/TLS behavior.
Song CDN and unrelated external hosts are not redirected.

## Automatic backend selection

Put one APK at `dist-unified/game.apk` and run `dist-unified/RUN_AUTO.cmd`, or
pass an APK path to `RUN_AUTO.cmd`.

Normal priority:

1. x86: `lib/x86/libcocos2dcpp.so` or `lib/x86/libgame.so`
2. legacy ARM: `lib/armeabi/libgame.so`
3. ARMv7/2.2: `lib/armeabi-v7a/libcocos2dcpp.so`

`OVERRIDE_ARM=true` changes only the first choice. It does not force ARM when
the APK contains no supported ARM library.

## One save folder

Every automatic and generated backend launcher runs from `dist-unified/` and
uses:

```text
dist-unified/save/
```

Because different Geometry Dash versions can reuse the same save-file names,
back up `save/` before switching between substantially different APKs.

## Source layout

- `src/backends/x86/` — alpha3 native x86 loader/JNI/runtime.
- `src/backends/arm_legacy/` — DynarmicTest14-fix1 ARMv5TE/Thumb backend.
- `src/backends/armv7/` — Milestone1 ARMv7/Thumb-2 backend.
- `src/shared/` — storage, audio, APK audio extraction, networking translation,
  launch settings, and build metadata shared across backends.
- `cmake/` — one build graph for both ARM generations and one Dynarmic checkout.

## Build

- `BUILD_X86.cmd` builds x86.
- `BUILD_DYNARMIC.cmd` builds both Dynarmic backends.
- `BUILD_ALL.cmd` builds all three and copies the configurable automatic
  launcher into `dist-unified/`.

Normal ARM launchers avoid full tracing. Use each backend's `RUN_DEBUG.cmd` only
for regression logs. The old F2 editor shortcut remains completely removed.
