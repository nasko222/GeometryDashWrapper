# Geometry Dash ARM Wrapper — 2.2 beta ARMv7 Bringup13

This is the separate Dynarmic ARMv7 branch for the supplied Geometry Dash 2.2
beta APKs. The stable Dynarmic Test14-fix1 line for Geometry Dash 1.0–1.4 is
unchanged.

## Bringup13 changes

- Removes the old 1 MiB guest C-string limit that truncated Press Start and
  Knock Em Out level data.
- Restores editor-object rendering with one targeted visibility hook.
- Restores the existing platformer controls' rendering with one targeted
  visibility hook.
- Sends Space/Up directly as platformer jump button 1.
- Plays WAV effects through `waveOut`; music continues through MCI.
- Keeps full companion `ApplyHooks`, DPAD touch hooks, collision hooks, and
  companion constructors disabled.

## Runtime design

- ARMv7 game code runs through Dynarmic on 64-bit Windows.
- The editor uses the targeted `LevelEditorLayerExt::initH` bridge.
- Valid level setup strings from the game are never replaced.
- APK catalog data is decoded lazily only to recover an empty setup.
- A verified latest-inflate payload is the fallback for custom/editor levels.
- Guest strings are bounded by mapped memory and a 64 MiB safety ceiling.

## Run the prebuilt wrapper

1. Extract the runtime archive.
2. Copy your selected beta APK beside the executable as `game.apk`.
3. Run `RUN_V22_SELECTED_APK.cmd`.

No APK is included.

## Build from source

On 64-bit Windows:

```bat
BUILD_V22BETA_X64.cmd "D:\path\to\your-beta.apk"
```

Output:

```text
dist-arm-wrapper-v22beta-bringup13\
```

## Controls

- Jump: mouse, Space, or Up Arrow
- Platformer left: A or Left Arrow
- Platformer right: D or Right Arrow

## Focused runtime test

1. Open the editor, place several objects, and confirm they render.
2. Playtest, pause/resume, and return to the editor.
3. Open a platformer level and test left, right, jump, and the button overlay.
4. Launch Power Trip, Knock Em Out, and Press Start.
5. Confirm level-loading and death sound effects play.
6. Switch repeatedly between those levels and a normal official level.

Attach `gd-v22beta-selected.log` if a step fails.
