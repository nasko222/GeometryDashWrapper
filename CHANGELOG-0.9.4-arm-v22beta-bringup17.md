# 0.9.4-arm-v22beta-bringup17

Branch: `v22beta-bringup17-safe-companions`

- Redirects every Android `/data/data/<package>/...` path into the portable
  `save-v22beta` directory.
- Adds `RECOVER_V22_SAVES.cmd` for listing/copying old drive-root saves before
  rebuilding.
- Recovers the newest misplaced `CCGameManager.dat`, `CCLocalLevels.dat`, and
  backup files from drive-root `data\data\<package>` directories when the local
  destination is absent. Recovery is copy-only.
- Adds targeted PauseLayer and EndLevelLayer Edit callback bridges to the
  already validated editor entry path.
- Keeps the selected beta's companion `LevelEditorLayerExt::initH` capability
  gated; no `libgame.so` constructors or global `ApplyHooks` pass are run.
- Inventories every packaged ARM `.so`, while treating `libhooking`, Dobby,
  legacy `libgdkit`, and unrecognized libraries as audit-only.
- Preserves Bringup16 platformer mouse/keyboard and editor-playtest fixes.
- Does not claim full editor support for stock SubZero or base-stub-only APKs.
