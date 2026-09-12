# Geometry Dash Wrapper 0.9.7-newera18

Geometry Dash Wrapper runs selected historical Android Geometry Dash builds as native Windows desktop programs. It does not emulate Android as a complete operating system.

## What newera18 changes

- Reverts the broken newera17 live-player camera pivot. Cube camera Y no longer reads player Y, so jumping cannot drag the viewport vertically.
- Keeps the exact newera15 mode-aware camera as the base, then applies a true 0.90 screen-space zoom-out.
- Cube zoom pivot is fixed at the historical floor line Y=90; constrained ship/ball/UFO modes zoom around gameplay-area center Y=160.
- Removes the inverse proxy counter-scale from newera17. Player proxies now obey the exact same camera zoom as level objects and breadcrumbs, so they get smaller with the world instead of appearing oversized.
- Keeps the x86 slider safety work: the editor horizontal Slider remains untouched by playtest suspend/restore and no forced updateSlider() is used.
- Launcher defaults remain: BOOMLINGS has SHOW_COMMAND_PROMPT=TRUE and OLD_VER_PLAYTEST=TRUE; GDPS keeps both FALSE.

## What newera17 changes

- **Reverts newera16 player/framing displacement back to the validated newera15 camera first**, then applies zoom mathematically around the player's already-correct screen position. Cube and constrained-mode placement therefore start from the exact newera15 transform.
- The editor gameplay world and green breadcrumb overlay use **0.90x zoom**, but camera translation is compensated by `(oldScale - zoomScale) * playerWorldPosition`. The player's screen coordinate does not move when zoom is enabled.
- The proxy root is counter-scaled by `1 / 0.90`, cancelling the parent camera zoom. This preserves the newera15 cube/ship/ball/UFO icon size, vehicle offsets and margins while the world itself shows more area.
- Removes newera16's fixed cube-floor and constrained-mode `+8` camera shifts. Those shifts changed the player/world alignment and are no longer used.
- **x86 freeze/purple-control fix:** the horizontal editor `Slider` is no longer disabled, retained, restored, or resynchronized at all. Ordinary descendant `CCMenu`s remain suspended/restored so gameplay clicks cannot trigger editor buttons underneath. This removes the purple horizontal control from the risky Stop path without giving up menu isolation.
- `EditorUI::updateSlider()` remains disabled on the x86 stop path.
- `RUN_AUTO_BOOMLINGS.cmd` now defaults `SHOW_COMMAND_PROMPT=TRUE` and `OLD_VER_PLAYTEST=TRUE`; `RUN_AUTO_GDPS.cmd` intentionally keeps both `FALSE`.
- Dynarmic builder revision is bumped to **129**.

## What newera16 changes

- **Camera-only follow-up to newera15.** Proxy composition, icon offsets, mode margins, physics and player world positions are unchanged.
- x86 and legacy ARM now apply a **0.90x camera zoom-out** to both the editor game layer and the scene-root proxy/trail overlay, keeping them perfectly aligned.
- Cube zoom is anchored on the historical **y=90 floor line**. The floor stays fixed while the cube sits slightly lower on screen after zoom-out; cube still does not vertically follow the player.
- Ship, ball and UFO continue using the real hidden PlayLayer `CCCamera` vertical restriction from newera15. After that authoritative camera is read, the view is zoomed around y=160 and lifted **8 points** so the lower game area clears the editor object selector.
- Horizontal zoom is anchored at x=120, preserving the established player framing while revealing more world.
- x86 wrapper Play/Pause mouse clicks are consumed by the host before Cocos touch dispatch, preventing the editor control underneath from entering a selected/purple state.
- x86 no longer calls `EditorUI::updateSlider()` synchronously after playtest stop; that forced resync is removed from the freeze-sensitive stop path.
- Existing retained editor-menu/slider suspension during gameplay and the zero-teardown stop strategy are otherwise unchanged.
- Dynarmic builder revision is bumped to **128**.

## What newera15 changes

- Restores **cube editor playtest camera behavior to the pre-newera11 path** on x86 and legacy ARM: horizontal scrolling uses the original `x = 120 - playerX` anchor, vertical translation comes from the hidden PlayLayer game-layer baseline, and the newera11+ global playtest zoom is not applied.
- Ship, ball, and UFO no longer use the reconstructed player-following camera from newera14. The wrapper now reads the hidden historical PlayLayer's real Cocos `CCCamera` and mirrors its **vertical viewport** while preserving the old horizontal editor framing. This lets the game itself choose the constrained top/bottom play area.
- If an unusually old image does not export `CCNode::getCamera` / `CCCamera::getCenterXYZ`, constrained modes fall back to a top/bottom dead-zone clamp. The fallback moves the viewport only when the player would leave the allowed area; it never centers the camera on the player.
- Removes newera14's standalone cube `+5` world-Y proxy correction. The proxy root is again placed at the raw `PlayerObject` world position, matching the pre-newera11 bridge.
- Keeps newera14's x86 pause/stop safety fix: disabled editor controls are retained and restored directly rather than recursively walking a rebuilt old Cocos tree.
- Trajectory prediction remains fully removed; reduced green-path sampling, first-attempt preservation, mirror suppression, and zero-teardown playtest parking remain intact.
- Dynarmic builder revision is bumped to **127**.

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
| `SHOW_COMMAND_PROMPT` | Script-specific | `RUN_AUTO_BOOMLINGS.cmd` defaults to `TRUE`; `RUN_AUTO_GDPS.cmd` defaults to `FALSE`. Controls whether the launcher console stays visible. |
| `OLD_VER_PLAYTEST` | Script-specific | `RUN_AUTO_BOOMLINGS.cmd` defaults to `TRUE`; `RUN_AUTO_GDPS.cmd` defaults to `FALSE`. Adds the inline editor play/stop control to Geometry Dash 1.0-1.7 on legacy ARM/x86. |
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

1. Read the live editor's unsaved level string, create a private temporary `GJGameLevel`, copy the playback metadata available in that build, and give only the clone to `PlayLayer`. The editor's own level object is not handed to gameplay.
2. Before PlayLayer creation, snapshot the editor's `GameManager::m_playLayer` and **edit-mode state**. Run the authentic old `startGame()` / `resetLevel()` path while temporarily suppressing only `updateAttempts()` for the startup reset, preserving Attempt 1 without skipping spawn-queue initialization.
3. Keep the real `PlayerObject` in its original hidden PlayLayer hierarchy for physics. A scene-root proxy mirrors its transform, colors, selected icon, and supported cube/ship/ball/UFO sprite family. Ship mode includes the selected cube inside the ship.
4. Reconstruct the validated newera15 camera first, then zoom the editor world / breadcrumb overlay to **0.90x around the current player world point**. Translation compensation keeps the player at the exact same screen coordinate it had before zoom; the proxy root is counter-scaled so its on-screen size and local vehicle/icon layout also remain unchanged.
5. Breadcrumb and proxy visuals live outside `LevelEditorLayer` / `EditorUI`. The green breadcrumb uses connected solid `square.png` segments, sampled every 16 world units and capped at 256 segments to cut sprite churn. The experimental orange trajectory predictor has been removed.
6. Suppress `EndPortalObject::triggerObject()` and keep the hidden end portal far ahead as a secondary guard, so inline editor testing has no normal level-complete endpoint. Where available, also suppress `PlayLayer::toggleFlipped(bool,bool)` so mirror portals have no effect in the editor test.
7. While playtest is active, save and disable EditorUI/LevelEditorLayer root touch handling. On x86, ordinary descendant `CCMenu`s are also suspended, but the horizontal `Slider` is deliberately never disabled/retained/restored because that path caused it to return selected/purple and could freeze the app after Stop.
8. Clicking the wrapper pause button or pressing Escape stops music, restores the original camera, root editor touch state, retained menu enabled states, `GameManager::m_playLayer`, **GameManager edit mode**, and end/mirror patches. The x86 path does not force `EditorUI::updateSlider()` and never mutates/restores the horizontal Slider.
9. The retired hidden PlayLayer remains attached, invisible, unscheduled and touch-disabled until scene destruction instead of running fragile old `onExit` teardown. If the backing player genuinely dies or is replaced, the bridge stops the test rather than allowing the historical retry/scene lifecycle into the editor.

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


## newera14 editor-playtest changes

- Replaced the broken newera13 `gameLayer->getPosition()` camera mirror with the actual old gameplay camera model. The playtest now follows horizontally, uses the 90/120 vertical dead band, and smooths vertical motion instead of remaining frozen at the editor start view.
- The 0.90 zoom is applied after reconstructing the world camera, with identical transforms on the editor game layer and the scene-root proxy/path overlay.
- Standalone cube proxy receives a +5 world-unit visual alignment correction; vehicle modes keep their local body/inner-cube layout.
- x86 editor control restoration no longer traverses the editor scene tree during stop. Cached controls are retained before disabling, restored directly, and released, targeting the pause/stop AV that occurred immediately after edit-mode restoration.
- Trajectory rendering remains completely removed.

## newera13 editor-playtest changes

- Camera no longer invents a fixed player anchor. It mirrors the real hidden `PlayLayer` game-layer X/Y camera every frame, then applies a 0.90 visual zoom around the 570x320 design center. This preserves the original cube/ship/ball/UFO camera clamps and vertical restrictions instead of fighting them.
- Player proxy sprites now live under one local-space root. Ship/UFO vehicle and cube offsets therefore rotate with the player rather than being applied as world-space Y offsets.
- Vehicle composition is closer to the original `PlayerObject`: cube local position `(0,+5)` at `0.55x`, ship body `(0,-5)`, UFO/bird body `(0,-7)`. Newer x86 builds also use their secondary vehicle/detail frames when available.
- Proxy mode/icon probing is reduced to once every four frames. The green path samples every 16 world units and caps at 256 segments, substantially reducing guest calls and draw-node growth.
- `RUN_AUTO_GDPS.cmd` and `RUN_AUTO_BOOMLINGS.cmd` now use uppercase `TRUE` / `FALSE` consistently for every boolean setting.
- The trajectory predictor remains fully removed. First-attempt preservation, edit-mode restoration, editor-control suspension, mirror suppression and stop-path safety are unchanged.

## newera12 editor-playtest changes

- Removed the experimental red/orange trajectory predictor completely.
- Editor playtest camera is now 15% zoomed out (`0.85x`) while preserving follow and exact camera restoration.
- Green breadcrumb rendering is sampled every 8 world units and capped at 1024 connected segments, dramatically reducing Cocos sprite churn while keeping the line continuous.
- Ship and UFO proxies render their selected cube above the vehicle instead of hiding it behind the vehicle sprite.
- Stop/pause restoration validates cached editor menu/slider nodes are still live descendants before calling into them, avoiding the x86 stale-menu crash seen in newera11-fix1.
