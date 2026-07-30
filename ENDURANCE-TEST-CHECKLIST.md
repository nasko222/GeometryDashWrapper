# EnduranceTest10 Test Sheet

## Selected 2.2 beta editor

- [ ] Open the editor and confirm BPM guidelines still render.
- [ ] Press Play and verify the right-side black region does not begin moving.
- [ ] Let playtest run long enough that the old region would wrap/restart.
- [ ] Stop and restart playtest; background state remains fixed both times.
- [ ] Change every ground option. Entries above 18 clamp to ground 18 and do not
      crash or freeze.
- [ ] Change every background option. Entries above 26 clamp to background 26
      and do not create a missing texture.
- [ ] Pan/zoom and use swipe selection; objects and the selection rectangle remain.
- [ ] Confirm normal level gameplay backgrounds still use the original movement.

Search the log for:

```text
DYNARMIC_V22_ART_ASSET_LIMITS ground=18 background=26
DYNARMIC_V22_EDITOR_BACKGROUND_UPDATE_SUPPRESSED
DYNARMIC_V22_BATCH_BLEND_HOST_EXACT
DYNARMIC_V22_EDITOR_TIME_MARKERS_REFRESH mode=level-settings-updated
```

`DYNARMIC_V22_BATCH_BLEND_MISSING_TEXTURE` should not appear during ordinary
background/ground selection after the selector clamp.

## Legacy ARM

- [ ] Music behavior is unchanged from EnduranceTest9.
- [ ] Z/X remain Practice Mode-only and functional.
- [ ] Run 1.3 long enough to check whether the earlier random freeze repeats.

## Regression checks

- [ ] x86 smoothness and Z/X unchanged.
- [ ] Backups/login/online levels unchanged.
- [ ] Version-isolated saves unchanged.
- [ ] Mouse and pause button remain native/visible.
