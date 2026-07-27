# Geometry Dash 2.2 beta ARMv7 Bringup17

Branch: `v22beta-bringup17-safe-companions`

## Findings from Bringup16 logs and APKs

### Saves

Bringup16 translated only paths beginning with
`/data/data/com.robtopx.geometryjump/`. The selected beta instead opens
`/data/data/com.gdpsedi.geometrydashsubzero/`, while stock SubZero uses another
package name. Windows therefore received the absolute-looking Android path
unchanged and resolved it beneath the current drive root, typically:

```text
D:\data\data\com.gdpsedi.geometrydashsubzero\
M:\data\data\com.robtopx.geometrydashsubzero\
```

Bringup17 redirects all `/data/data/<package>/...` paths to `save-v22beta` and
copies the four normal save/backup files from legacy drive-root locations when
the portable destination is absent. Migration is copy-only; it does not erase
the misplaced backup.

### Gameplay Edit buttons

The selected primary library's `PauseLayer::onEdit(CCObject*)` and
`EndLevelLayer::onEdit(CCObject*)` are two-byte stubs. Their `goEdit` methods
attempt the primary `LevelEditorLayer` path, whose initializer is also only a
base stub in this Android build. The companion `libgame.so` supplies a complete
`LevelEditorLayerExt::initH` and pause/end fix routines, but those routines
normally depend on global hooks having replaced the primary editor initializer.

Bringup17 patches only the callback pointers for Pause and EndLevel Edit. The
host retrieves the current `GJGameLevel` from GameManager/PlayLayer and enters
the existing validated editor path, directly invoking companion `initH` when
its ABI matches. This avoids executing the companion's global hook manager.

### Native libraries

Selected late beta:

```text
libcocos2dcpp.so
libfmod.so
libgame.so
libhooking.so
libdobby.so
```

The other supplied beta additionally uses legacy `libgdkit.so`. The hook-engine
libraries are useful dependencies for the original Android mod loader, but they
do not by themselves provide a missing stock SubZero editor. `libgame.so`
contains many unrelated hook groups, so loading all constructors is not a safe
shipping default.

## Safety boundary

- Map and execute the primary `libcocos2dcpp.so` normally.
- Map a companion `libgame.so` only when recognized, and invoke only its
  ABI-validated editor initializer.
- Inventory `libhooking.so`, `libdobby.so`, `libgdkit.so`, and other `.so`
  files, but do not run their constructors automatically.
- Keep complete `ApplyHooks`, DPAD, CollisionFix, ShaderFix, SpeedrunTimer,
  GDPSManager, Options, Hacks, Servers, Emojis, search, icon, and debug hook
  sets disabled in the stable friend-test package.

## Validation available in this source handoff

- C++20 syntax check of `src/dynarmic_probe.cpp`: passed using interface stubs
  matching the pinned Dynarmic API surface.
- Selected primary ELF callback pointer audit: both Pause and EndLevel stub
  addresses have aligned data references that the targeted bridge can patch.
- APK/library and log inspection: completed for selected late beta, early beta,
  and stock SubZero.
- Windows x64 cross-link and live gameplay were not run in this Linux session;
  build and execute on Windows before calling the binary release-tested.
