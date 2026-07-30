# EnduranceTest8 Test Sheet

## Selected 2.2 beta editor

- [ ] Open editor and confirm BPM guidelines render.
- [ ] Press Play and verify the right-side black region never begins moving.
- [ ] Pan/zoom and use swipe selection; objects and selection rectangle remain.
- [ ] Change background texture, play, stop and repeat; background stays stable.
- [ ] Change ground texture repeatedly; no crash.
- [ ] Confirm gameplay backgrounds still scroll outside the editor.

Search the log for:

```text
DYNARMIC_V22_EDITOR_BACKGROUND_FROZEN
DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH mode=level-settings-updated
DYNARMIC_V22_NULL_BATCH_BLEND_GUARD
```

## Legacy ARM

- [ ] 1.0, 1.01, 1.3 and 1.4: compare attempt 1 with attempt 2+.
- [ ] Menu music volume remains normal.
- [ ] Z/X remain Practice Mode-only and functional.
- [ ] Run 1.3 long enough to check whether the earlier random freeze repeats.

Expected level-start audio diagnostic:

```text
Audio legacy first-play loudness: <track> volume=1000
```

## Regression checks

- [ ] x86 smoothness unchanged.
- [ ] Backups/login/online levels unchanged.
- [ ] Version-isolated saves unchanged.
- [ ] Mouse and pause button remain native/visible.
