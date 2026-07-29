# EnduranceTest1

## Scope

This branch is for long cross-version testing. It deliberately freezes the
Fix6 x86 pacing implementation and changes only four active areas:

1. cursor state around pause menus;
2. top-right pause-button presentation;
3. legacy account backup transport;
4. late-beta editor overlays and visibility cleanup.

## 1.93 backup diagnosis

The failed 1.93 session established synthetic port-80 connections but never
produced a complete `backupGJAccount` request or a WinHTTP completion line.
Login and later sync requests in the same test archive did reach the server and
returned HTTP 200.

Large account backups may split the HTTP headers and body and use
`Expect: 100-continue`. EnduranceTest1 buffers the incomplete request, returns
an interim `HTTP/1.1 100 Continue`, then joins the following body before
handing the complete request to WinHTTP.

This is a targeted compatibility fix, not a claim that every server-side
backup failure is impossible. The run log now records the interim response and
pending byte count.

## Editor exact-visibility mode

Fix6's host approximation restored object colouring, but it did not implement
all work performed by the beta extension. EnduranceTest1 defaults to the
companion's full `LevelEditorLayerExt::updateVisibilityH` routine and supplies
its original-function pointer.

Expected targets:

- song-position line;
- BPM guidelines;
- right-edge clip/camera cleanup after playtesting;
- existing black sawblade/monster/death-X colouring remains correct.

Toggle:

```bat
set "V22_EXACT_EDITOR_VISIBILITY=true"
```

Use `false` only for an A/B comparison with the Fix6 approximation.

## Pause/cursor behavior

- Escape still opens the full pause menu.
- The cursor remains visible while interacting with that menu.
- The late-beta visual pause item is hidden separately from its callback.
- Gameplay keyboard input returns to hidden-cursor mode.
- x86 pause-item hiding is guarded by RTTI because old x86 releases do not all
  use one stable UILayer layout.
- Legacy ARM retains the safe top-right click suppression; no guessed legacy
  object-field write was added.

## Endurance testing

Test one APK per run and keep the generated dated log directory.

Recommended order:

1. 1.0, 1.01 and 1.4 legacy ARM;
2. 1.6, 1.8, 1.9, 1.93, 2.0, 2.1 and 2.11 x86;
3. World, Lite, Meltdown and SubZero;
4. selected 2.2 beta editor for at least 20 minutes;
5. repeat 1.93 backup after a fresh login.

For the editor, test song-only playback, BPM guidelines, long horizontal
playtesting, Stop, pan/zoom and repeated pause/resume.
