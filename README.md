# Geometry Dash Wrapper 0.9.5 — EnduranceTest3

`endurancetest` is the cross-version stability branch. EnduranceTest3 is a
narrow recovery from EnduranceTest2: it restores the accepted Fix6 x86 host
loop, keeps the now-working old-client account backup transport, corrects
cursor behavior by separating editors from player gameplay, hides verified
legacy ARM pause controls, and moves the 2.2 editor timeline update into the
actual per-frame render path.

## Running

Python is not required.

- `RUN_AUTO_GDPS.cmd` uses `naskogdps17.7m.pl/database`.
- `RUN_AUTO_BOOMLINGS.cmd` uses `www.boomlings.com/database`.

Double-click a launcher to use `game.apk`, or drag an APK onto either launcher.
The native launcher selects the backend and creates the package/version/backend
save profile and dated log folder.

## Default settings

```bat
set "DISABLE_PAUSE_BUTTON=true"
set "HIDE_CURSOR_DURING_PLAY=true"
set "VERSION_ISOLATED_SAVES=true"
```

Version-isolated saves remain toggleable and enabled by default. Setting
`VERSION_ISOLATED_SAVES=false` restores the old flat `save\` root. Existing
flat saves are not guessed or migrated automatically.

## EnduranceTest3 changes

- Restores the Fix6 x86 `main.c` host loop and removes EnduranceTest1/2's
  recursive Cocos scene traversal. That traversal ran inside the frame loop and
  is the most likely source of the severe x86 regression and incorrect cube
  animation state.
- Keeps the Fix6 x86 frame-pacing function exactly unchanged.
- Keeps EnduranceTest2's successful `100 Continue`/pending-body backup repair.
- Keeps the cursor visible in editors. In player gameplay it hides again after
  Resume, including legacy ARM and ARMv7.
- Hides the verified legacy ARM top-right pause control through the game's own
  `CCNode::setVisible(false)` method; Escape still opens the normal pause menu.
- Keeps the confirmed 2.2 object-colour repair for sawblades, monsters and the
  editor death X.
- Drives `DrawGridLayer::updateMusicGuideTime` once per rendered editor frame,
  periodically refreshes BPM/time markers, and refreshes the editor grid when
  playtest starts or stops. This replaces the intermittent visibility-callback
  update that made the song line appear but remain frozen.
- The freezing exact companion visibility redirect remains disabled internally;
  there is no launcher toggle for it.
- Contains no Python files and no `.gitignore`.

## Still requiring runtime confirmation

The 2.2 song line, BPM guides and moving right-side black region are targeted
but not claimed fixed until tested on Windows. The grid refresh on playtest
transitions is aimed at stale editor state after Stop, without restoring the
exact companion routine that froze the editor.

## Code guide

Start with `docs\CODE-TOUR-FOR-CSHARP-JAVA-DEVS.md`.
