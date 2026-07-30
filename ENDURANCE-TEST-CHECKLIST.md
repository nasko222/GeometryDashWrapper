# EnduranceTest7 Test Sheet

## Confirmed controls — regression only

- [ ] Normal Mode: Z/X do nothing.
- [ ] Practice Mode: Z places and X removes one checkpoint per press.
- [ ] Repeat briefly on legacy ARM, older x86 and ARMv7 2.2.

No Practice Mode implementation changed in this build.

## 2.2 beta editor

- [ ] Open an editor level with BPM enabled: guidelines appear immediately.
- [ ] Song-position line appears and moves.
- [ ] Close and reopen the editor: BPM guidelines and song line return.
- [ ] Press Play, Stop, pan left/right, zoom and use swipe selection.
- [ ] Placed objects and the swipe-selection rectangle remain visible.
- [ ] Record whether the moving right-side black region appears.
- [ ] Keep the editor open for at least 10 minutes.

Expected new log entries:

```text
DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH mode=session-once frame=1
DYNARMIC_V22_EDITOR_CAMERA_CULL ... opacity=0:...,70:...,255:... hide-0121=...
```

There must be no host `DYNARMIC_V22_EDITOR_GRID_REFRESH` call.

## Legacy music

- [ ] Compare attempt 1 and attempt 2 with the same Windows output volume.
- [ ] Compare attempts 3–5.
- [ ] Test Restart, death restart and pause/resume.
- [ ] Change the music slider while a stream is open and confirm the current
      stream responds.

Expected behavior: newly opened tracks receive no automatic `setaudio` command;
only a live volume setter for the currently open alias sends one.

## Regression checks

- [ ] Mouse remains native-visible.
- [ ] Native pause button remains present.
- [ ] 1.93 backup/load remains functional.
- [ ] 2.11 save/load remains functional.
- [ ] x86 pacing remains identical to EnduranceTest6.
