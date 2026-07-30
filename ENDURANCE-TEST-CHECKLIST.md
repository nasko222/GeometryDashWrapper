# EnduranceTest11 Test Sheet

## Selected 2.2 beta editor

- [ ] Open the editor and verify the black moving region is still gone.
- [ ] Confirm BPM guidelines, song line and swipe-selection rectangle still work.
- [ ] Cycle through every valid ground normally.
- [ ] Select one of the community entries that previously froze.
- [ ] The editor remains responsive and packaged ground 1 is used as fallback.
- [ ] Stop/restart playtesting and confirm the background remains fixed.

For the invalid entry, search the log for:

```text
DYNARMIC_V22_MISSING_GROUND_TEXTURE_REPLACED
```

`DYNARMIC_V22_BATCH_BLEND_MISSING_TEXTURE ... fallback-load-failed` should not
appear in the successful fallback case.

## Legacy ARM music

- [ ] Test 1.0, then preferably 1.3/1.4.
- [ ] Enter a level from the menu and compare attempt 1 with attempt 2.
- [ ] Confirm the one-time prime is inaudible and does not add a long delay.
- [ ] Confirm menu music, pause/resume and the volume slider still behave.

Search the log for:

```text
Audio legacy first-play decoder prime: 100 ms muted
```

## Regression checks

- [ ] Practice Z/X remains functional and Practice Mode-only.
- [ ] x86 smoothness and behavior remain unchanged.
- [ ] Backups/login/online levels remain unchanged.
- [ ] Version-isolated saves remain unchanged.
- [ ] Mouse and pause-button behavior remains native.
