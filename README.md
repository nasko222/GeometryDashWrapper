# Geometry Dash Wrapper 0.9.7-newera9

Geometry Dash Wrapper runs selected historical Android Geometry Dash builds as native Windows desktop programs. It does not emulate Android as a complete operating system.

## What newera9 changes

- The confirmed first-attempt fix is retained unchanged.
- Fixes the still-dotted green/orange lines: `square.png` is 32 physical pixels in the HD asset set but 16 Cocos points at the active content scale. Newera8 stretched each segment as if it were 32 points wide, so it only covered about half the requested distance. Newera9 scales against 16 points and overlaps joins slightly.
- Replaces the red-orange predictor that re-anchored itself to the player every frame. Cube/ball predictions now snapshot an airborne launch and remain fixed in world space until landing, gravity change, or mode change. Ship/UFO prediction is hidden for now because those modes are continuously input-driven and the old sampled ballistic approximation was visibly wrong.
- Changes old-editor playtest stop to a zero-teardown parking path. The supplied ARM 1.1 log still crashes during the first post-play placement at `strlen(0x210)` even after `removeFromParentAndCleanup(false)`. Newera9 therefore does not detach the hidden PlayLayer and does not remove its proxy/trail nodes while the editor scene is alive. It makes the entire retired tree invisible, stops actions/schedulers, disables PlayLayer/UILayer touch and keypad handling, restores `GameManager::m_playLayer`, and leaves the inert tree scene-owned until scene destruction. This intentionally trades temporary memory for editor stability.
- Camera framing, player colors, ship-with-cube proxy, gamemode switching, endless playtest, music stop, play-button sizing, and the working Attempt 1 preservation remain unchanged from newera8.
- Dynarmic builder revision is bumped to 120 so the legacy ARM executable cannot be reused from the newera8 cache.

## Architecture

`GeometryDashLauncher.exe` parses the APK ZIP directory and binary `AndroidManifest.xml`, records package/version metadata, picks a backend, creates the save and log paths, exports shared settings, and starts the backend.

| APK library found | Selected backend | Execution model |
| --- | --- | --- |
| `lib/x86/libcocos2dcpp.so` or `lib/x86/libgame.so` | `x86` | Direct 32-bit ELF loading and relocation on Windows |
| `lib/armeabi/libgame.so` | `arm-legacy` | ARMv5/Thumb execution through Dynarmic on a 64-bit Windows host |
| `lib/armeabi-v7a/libcocos2dcpp.so` | `armv7` | ARMv7/Thumb-2, VFP, and NEON execution through Dynarmic |

If an APK contains x86 and ARM libraries, the launcher prefers x86. The wrapper maps Android/JNI and libc calls into Windows implementations, translates OpenGL calls while preserving the guest viewport and scissor state, and keeps input coordinates aligned with the letterboxed content area.

This is a compatibility project, not a promise that every APK carrying one of those ABI paths will work. Old releases differ at the binary level. The legacy backend includes audited handling for the earliest `libgame.so` line, including the verified 1.0 binary. The ARMv7 2.2-beta path is intentionally narrow: a compatible selected late-2023 build can use its validated companion `LevelEditorLayerExt::initH`, while the unstable wrapper-owned reconstruction for stock 2019/2022/2023 editor stubs remains retired.

## Running an APK

Build first, then either:

1. Drag an APK onto `RUN_AUTO_GDPS.cmd` to use the configured GDPS server.
2. Drag an APK onto `RUN_AUTO_BOOMLINGS.cmd` to use the official Boomlings endpoint.
3. Put the APK beside the wrapper as `game.apk` and double-click either script.

The launcher identifies Geometry Dash, Lite, World, Meltdown, and SubZero package names for the window title and icon. Press F11 or Alt+Enter to switch between windowed and fullscreen modes. A resized window keeps the guest aspect ratio and maps mouse/touch coordinates back to the guest surface.

## Configuration

Edit the `set "NAME=value"` lines near the top of the two `RUN_AUTO_*.cmd` files.

| Setting | Supplied script default | Behavior |
| --- | --- | --- |
| `GDPS_SERVER` | Script-specific | GD API host and base path. `RUN_AUTO_BOOMLINGS.cmd` uses `www.boomlings.com/database`; the GDPS script contains the private-server endpoint. |
| `HACK_ICONS` | `false` | Enables the supported icon-unlock patches. |
| `FULL_BYPASS` | `true` | Enables supported restriction/bypass patches for recognized builds. |
| `FORCE_HIGHEST_GRAPHICS` | `true` | Requests the highest packaged graphics tier. It is suppressed for the verified 1.0 legacy binary because that build crashes on the forced path. |
| `MUSIC_PULSE_MAX` | `0.30` | Clamps the music-derived pulse level from `0.0` to `1.0`. |
| `FPS` | `VSYNC` | `VSYNC` requests swap interval 1. A numeric value from `1` through `10000` disables VSync and uses the shared high-resolution host frame cap. Invalid values fall back to VSync. |
| `RESOLUTION` | `1140x640` | Logical/native render size passed to the game window. Keep the default for the historical layout, or override it with values such as `1280x720`. |
| `TEXTURE_FILTERING` | `GAME` | `GAME` preserves the guest's original magnification-filter requests. `LINEAR` smooths enlarged textures; `NEAREST` forces crisp pixels. |
| `ANTIALIASING` | `NONE` | Host antialiasing mode: `NONE`, `FXAA`, `MSAA2`, `MSAA4`, or `MSAA8`. Unsupported MSAA sample counts fall back to a lower count and then off. |
| `SHOW_COMMAND_PROMPT` | `FALSE` | Hides the launcher console. Set `TRUE` to keep it visible for diagnostics. Boolean settings also accept yes/no, on/off, and 1/0. |
| `OLD_VER_PLAYTEST` | `FALSE` | Adds the inline editor play/stop control to Geometry Dash 1.0-1.7 on the legacy ARM and x86 backends. |
| `VERSION_ISOLATED_SAVES` | `true` | Gives each package/version/backend combination its own save directory. Set `false` to use the shared `save` directory. |
| `EDITOR_CONTROLLS` | `true` | Enables legacy/x86 editor movement and rotation shortcuts. The historical misspelling is part of the public setting name. |
| `I_LOST_THE_GAME` | `true` | Launch guard set by the scripts. Direct backend execution without it shows the wrapper's launch message and exits. |

Advanced/internal settings:

- `V22_EXACT_EDITOR_VISIBILITY=TRUE` selects the complete companion visibility pass only on the validated late-2023 ARMv7 layout. It is not a general stock-beta fix.
- `GD_X86_API_CONNECT_MODE` is selected automatically for the x86 2.11 protocol path. It normally should not be overridden.
- `EXTRAS_MENU` is deliberately disabled in this branch even if an environment value is supplied.

### Texture filtering

`RESOLUTION` (default `1140x640`) and texture filtering are separate. `TEXTURE_FILTERING=GAME` is the default and leaves the guest's OpenGL requests untouched. `LINEAR` upgrades magnification requests from `GL_NEAREST` to `GL_LINEAR`; `NEAREST` forces linear magnification requests back to crisp nearest-neighbor sampling. This is texture sampling, not AI upscaling, and it cannot add artwork detail absent from the APK.

### Antialiasing

`ANTIALIASING=NONE` preserves the old behavior. `MSAA2`, `MSAA4`, and `MSAA8` request a multisampled WGL pixel format before the real OpenGL context is created; if the requested count is unavailable the wrapper tries lower sample counts before falling back to ordinary rendering. `FXAA` applies a lightweight GLSL post-process to the completed back buffer immediately before presentation. These options affect edge smoothing only and do not change `RESOLUTION` or texture data.

### Old-version playtest

With `OLD_VER_PLAYTEST=TRUE`, Geometry Dash 1.0-1.7 editors gain a small play control using the old build's own `GJ_playBtn2_001.png`. This remains an experimental compatibility bridge rather than Save & Play.

1. Read the live editor's unsaved level string, create a private temporary `GJGameLevel`, copy the playback metadata available in that build, and give only the clone to `PlayLayer`. The editor's own level object is not mutated.
2. Create the authentic old `PlayLayer`. Run the complete `startGame()` / `resetLevel()` initialization, while temporarily suppressing only `updateAttempts()` for that one reset. This initializes the spawn queues correctly without advancing the visible attempt counter.
3. Keep the real `PlayerObject` in its original hidden PlayLayer hierarchy for physics. A separate editor overlay mirrors its transform. The proxy changes sprite family when the real player enters ship, ball, or bird/UFO mode on builds that expose those modes.
4. Follow the real player's horizontal movement with the editor game layer and mirror the hidden gameplay camera vertically with no additional Y offset.
5. Breadcrumb, trajectory, and proxy sprites live in a scene-root sibling overlay, not under `LevelEditorLayer` or `EditorUI`. Green segments account for the HD content scale so adjacent solid squares overlap into one continuous line. Cube/ball trajectory is a fixed world-space launch prediction; ship/UFO trajectory is intentionally hidden until an input-aware predictor is implemented.
6. Suppress `EndPortalObject::triggerObject()` while testing and keep the hidden portal far ahead as a secondary guard, so inline testing has no normal level-complete endpoint.
7. Hide the play control while testing and show the pause control in the same toolbar slot. Clicking it or pressing Escape stops music and restores end-trigger/camera/GameManager state, but performs no PlayLayer or overlay removal. The retired PlayLayer, UILayer, proxy, trail, and private level are parked invisible and unscheduled under the current scene until scene destruction. This avoids invoking the old engine's `onExit`/cleanup path before the editor is finished with its own state.
8. If the backing player genuinely dies or is replaced, the bridge tears down the test rather than allowing the historical PlayLayer retry/scene lifecycle to leak into the editor.

The bridge covers ARM Geometry Dash 1.0-1.4 and x86 Geometry Dash 1.5-1.7 when the required exported game/Cocos2d-x interfaces are present. Missing capabilities fail closed and are logged. Runtime testing on the individual historical APKs is still required because their internal layouts differ by release.

## Keyboard and text input

| Context | Keys |
| --- | --- |
| Gameplay | Space or Up presses/releases the player input. |
| Platformer gameplay on the supported ARMv7 beta | A/D or Left/Right move; Space or Up jumps. |
| Practice mode | Z places a checkpoint; X removes the last checkpoint where the loaded build exposes the required callbacks. |
| Legacy/x86 editor with `EDITOR_CONTROLLS=true` | W/A/S/D move selected objects; hold Shift for the small step; Q/E rotate. |
| Active text field | Normal Windows character input, Backspace, Enter, and Ctrl+V. Paste is converted from UTF-16 clipboard text to UTF-8 before entering the game's JNI text path. |
| Any game scene | Escape is translated to Android Back. |
| Window | F11 or Alt+Enter toggles fullscreen. |

ARMv7 editor shortcuts remain game/companion-owned so A/D can continue to work as platformer controls without the wrapper guessing editor ownership.

## Graphics and windowing

The wrapper creates a Win32 OpenGL window, forwards the guest's fixed-function rendering calls, tracks guest viewport/scissor rectangles, and rescales them into the letterboxed client area. DPI awareness is enabled so Windows display scaling does not introduce a second blurry stretch. `RESOLUTION`, `TEXTURE_FILTERING`, and `ANTIALIASING` are independent: the first changes the guest surface, the second changes texture magnification sampling only when explicitly overridden, and the third controls host edge antialiasing.

The ARMv7 compatibility path retains the audited late-2023 ground/background bounds clamps and editor visibility/song-position behavior used by the supported companion build. It does not restore the retired host-built stock-beta editor.

## Audio

The shared Windows audio layer covers background music and effects without changing the original game assets:

- APK audio extraction and cache management;
- background playback and seek/replay behavior;
- asynchronous sound-effect playback;
- x86 FMOD-facing compatibility calls;
- per-wrapper software gain/mixer isolation so changing wrapper music volume does not alter the system-wide Windows mixer;
- custom-song metadata/download paths, including the official song endpoint fallback where required.

## Network and GDPS behavior

Known Geometry Dash API URLs, DNS targets, plaintext request lines, and Host headers can be rewritten onto `GDPS_SERVER` while preserving the endpoint path and query. The backends provide the socket/HTTP compatibility expected by their respective clients, apply bounded Windows network waits, and keep failed requests from permanently blocking the render loop. Official song metadata remains available as a targeted fallback instead of redirecting unrelated traffic.

The wrapper does not weaken HTTPS globally and does not bundle account credentials.

## Saves and logs

With the supplied scripts, saves are written below:

```text
save\{package}__v{version}__{backend}\
```

Setting `VERSION_ISOLATED_SAVES=false` writes to `save\` instead. The storage bridge maps Android writable paths into that directory, imports recognized legacy root `CC*.dat` files when safe, persists emulated preferences in `preferences.bin`, and commits game files through a temporary file plus write-through replacement.

Each launch gets a dated directory below `logs\`. It contains `run-info.txt`, the backend log, and—on Dynarmic backends—frame profiling/import diagnostics. `logs\latest-run.txt` points to the newest run directory. Use `SHOW_COMMAND_PROMPT=TRUE` and attach the newest log directory when reporting a launch failure.

## Building from source

Use a 64-bit Windows installation with PowerShell and Git available. The first build needs internet access for the pinned public tool/dependency downloads.

Run:

```bat
BUILD_ALL.cmd
```

Output is assembled in `dist-unified\` with the launcher, all three backends, run scripts, save directory, and icon assets. Individual entry points are also available:

- `BUILD_X86.cmd`
- `BUILD_DYNARMIC.cmd`
- `BUILD_LAUNCHER.cmd`

The Dynarmic build pins Zig 0.14.1, CMake 3.31.10, Ninja 1.13.2, Boost 1.84.0, and Dynarmic revision `a41c380246d3d9f9874f0f792d234dc0cc17c180`. Downloaded archives are checksum-verified. Build caches and tools remain local under `.build-tools` and `build-cache-windows`.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/launcher` | Native APK parser, metadata reader, backend selection, save/log setup, and process launch |
| `src/backends/x86` | Direct x86 ELF loader, JNI shim, Win32/OpenGL host, FMOD compatibility, and x86 runtime imports |
| `src/backends/arm_legacy` | Legacy ARM/Thumb Dynarmic executor and old-game compatibility paths |
| `src/backends/armv7` | ARMv7/Thumb-2 Dynarmic executor and selected 2.2-beta compatibility paths |
| `src/shared` | Settings, DPI/window helpers, storage, audio, songs, network compatibility, icons, and frame pacing |
| `assets/icons` | Window icons and source attribution |
| `third_party` | Vendored zlib and stb_vorbis sources with their licenses |
| `cmake`, `tools`, `build_*.ps1` | Reproducible Windows build orchestration and toolchain wrappers |

## Compatibility policy and engineering record

The active code follows observed ABI and binary behavior. Compatibility paths are keyed by library architecture, resolved exports, CRC/profile checks, validated object layouts, and instruction patches whose original bytes are checked before replacement. Unsupported capabilities fail closed and produce diagnostic results instead of being treated as present.

The abandoned 1.02 comments hotkey and the progressively reconstructed stock 2.2 editor were removed because runtime evidence did not support keeping them. The selected 2023 companion initializer, platformer input resolver, gameplay Edit callback, background/ground bounds checks, and frame pacing remain because their layouts and call paths were independently validated. Code comments are reserved for ABI constraints, compatibility traps, and other facts that are not apparent from the implementation.

## Known limitations

- The wrapper is Windows-only.
- It does not emulate a complete Android device or guarantee arbitrary APK/mod compatibility.
- The old-version inline playtest is experimental and currently limited to Geometry Dash 1.0-1.7 builds exposing the audited editor, play-layer, menu, and sprite interfaces.
- Cursor hiding is intentionally not included; previous attempts were unreliable and the feature remains postponed.
- Stock/reduced 2019, 2022, and 2023 2.2-beta APKs without the compatible companion initializer do not gain a wrapper-built editor.
- A Linux source audit cannot replace runtime testing of the generated Windows binaries.

## License

The wrapper's license is in `LICENSE`. Vendored zlib terms are in `third_party/ZLIB-LICENSE.txt`; stb_vorbis terms are in `third_party/stb/LICENSE`. Geometry Dash and its assets remain the property of their respective owners and are not distributed here.
