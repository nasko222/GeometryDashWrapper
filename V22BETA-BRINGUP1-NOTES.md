# Geometry Dash 2.2 Beta ARMv7 Bring-up 1

This is a separate experimental branch forked from DynarmicTest14. It does not replace the stable Geometry Dash 1.0-1.4 ARMv5TE branch.

## Input modes

### Raw native-library probe

The source package includes the supplied `libcocos2dcpp.so` at its root. Build normally:

```bat
BUILD_DYNARMIC_X64.cmd
```

Then run:

```bat
RUN_V22_RAW_SO_PROBE.cmd
```

Raw mode can validate ARMv7 execution, ELF relocation, all constructors, `JNI_OnLoad`, JNI discovery and imported APIs. It deliberately stops before `nativeSetApkPath`/`nativeInit`, because the Java classes and assets are absent.

### Complete APK bring-up

Pass the complete 2.2 beta APK to the same builder:

```bat
BUILD_DYNARMIC_X64.cmd "D:\path\to\geometry-dash-2.2-beta.apk"
```

The output packages it as `game-v22beta.apk`. Run:

```bat
RUN_V22_APK_INTERACTIVE.cmd
```

The executable automatically extracts `lib/armeabi-v7a/libcocos2dcpp.so` (with `lib/armeabi/libcocos2dcpp.so` as a fallback), sets the APK path, creates the OpenGL host, and attempts `nativeInit` and the normal render loop.

## Architecture changes

The supplied library is ARM EABI5, ARMv7-A, Thumb-2, VFPv3, NEONv1, soft-float ABI. This branch configures Dynarmic as `ArchVersion::v7A` and executes a startup smoke sequence containing Thumb-2, VFP and NEON instructions before loading the game.

## Native-library changes handled

- Library name/path changed from `lib/armeabi/libgame.so` to `lib/armeabi-v7a/libcocos2dcpp.so`.
- `nativeSetApkPath` is accepted as the path setter.
- The 2.2 `getFileDataFromZip` Thumb-2 prologue is hooked safely.
- `existFileDataFromZip` is no longer required because this library does not export it.
- The browser/openURL hook remains supported.
- 84 GLES imports have explicit argument descriptors, including stencil calls, integer uniforms, matrix-3 uniforms, depth mask and blend equation.
- 28 FMOD imports use an initial guest-object bridge. It creates guest System/Sound/Stream/Channel/DSP objects and maps basic playback, volume, pause, stop and position operations to the existing Windows audio bridge.
- All 260 non-GL/non-FMOD imports have an explicit handler or intentional compatibility result, so the supplied library has no known import that falls directly into the generic permissive fallback.

## Static validation performed in this package

The supplied raw library was parsed and relocated by the branch's own loader in static-audit mode:

```text
Image: 0x10000000-0x10933000
Constructors: 349
Function imports: 372
Imported objects: 5
Relocations: 103102
ZIP hooks: 1
openURL hooks: 1
GLES imports: 84
FMOD imports: 28
```

The raw `.so` has not been run through real Dynarmic in this Linux environment, and the complete APK was not supplied. The first Windows run may therefore expose an unsupported ARM instruction, a JNI method not yet modeled, or behavior that needs a more complete FMOD bridge. The log is designed to identify the exact first boundary.
