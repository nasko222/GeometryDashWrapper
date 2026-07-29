# EnduranceTest5 Test Sheet

## Z/X safety

- [ ] Normal Mode: Z does not create a checkpoint.
- [ ] Normal Mode: X does not remove anything.
- [ ] Practice Mode: Z creates one checkpoint per press.
- [ ] Practice Mode: X removes the latest checkpoint per press.
- [ ] Hold Z/X: auto-repeat does not create/remove repeatedly.
- [ ] Repeat on legacy ARM 1.0, 1.01 and 1.4.
- [ ] Repeat on x86 1.6–2.11.
- [ ] Repeat on selected ARMv7 2.2 beta.

Expected normal-mode log:

```text
DYNARMIC_PRACTICE_HOTKEY_IGNORED ... mode=normal
```

## 2.2 editor

- [ ] Open editor: song line appears and moves.
- [ ] BPM guidelines appear.
- [ ] Press Play, Stop, then inspect every placed object.
- [ ] Objects do not disappear after repeated Play/Stop cycles.
- [ ] Close and reopen editor: song line and BPM guidelines appear again.
- [ ] No `DYNARMIC_V22_EDITOR_GRID_REFRESH` line appears.
- [ ] Record whether the right-side black region still appears.

## Legacy music

- [ ] Attempt 1 volume.
- [ ] Attempt 2 volume.
- [ ] Attempts 3–5 volume.
- [ ] Pause/resume volume.
- [ ] Restart-level volume.

Expected guard log after a restart:

```text
Audio MCI volume guard active: volume=... frames=45
```

## Regression checks

- [ ] Mouse remains native-visible.
- [ ] Native pause button remains present.
- [ ] 1.93 backup and load remain functional.
- [ ] 2.11 save/load remains functional.
- [ ] x86 pacing remains identical to EnduranceTest4/accepted Fix6.
