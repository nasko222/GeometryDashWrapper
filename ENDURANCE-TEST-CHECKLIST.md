# EnduranceTest9 Test Sheet

## Selected 2.2 beta editor

- [ ] Open editor and confirm BPM guidelines still render.
- [ ] Press Play and verify the right-side black region stays stationary.
- [ ] Let playtest run long enough that the old region would wrap/restart.
- [ ] Change background texture and repeat; a replacement node is recaptured.
- [ ] Change every available ground/background option; unsupported community
      entries may show blank/default art but must not crash or freeze.
- [ ] Pan/zoom and use swipe selection; objects and selection rectangle remain.
- [ ] Confirm gameplay backgrounds still scroll outside the editor.

Search the log for:

```text
DYNARMIC_V22_EDITOR_BACKGROUND_ANCHOR_CAPTURE
DYNARMIC_V22_EDITOR_BACKGROUND_POSITION_RESTORED
DYNARMIC_V22_BATCH_BLEND_HOST_EXACT
DYNARMIC_V22_BATCH_BLEND_SAFE_FALLBACK
DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH mode=level-settings-updated
```

## Legacy ARM

- [ ] Confirm the EnduranceTest8 `volume=1000` diagnostic is completely absent.
- [ ] Menu/level music behavior is otherwise unchanged from the normal bridge.
- [ ] Z/X remain Practice Mode-only and functional.
- [ ] Run 1.3 long enough to check whether the earlier random freeze repeats.

## Regression checks

- [ ] x86 smoothness and Z/X unchanged.
- [ ] Backups/login/online levels unchanged.
- [ ] Version-isolated saves unchanged.
- [ ] Mouse and pause button remain native/visible.
