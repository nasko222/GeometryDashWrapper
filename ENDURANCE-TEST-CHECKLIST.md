# EnduranceTest12 Test Sheet

## Selected 2.2 beta editor

- [ ] Confirm the black moving region remains gone.
- [ ] Confirm BPM guidelines, song line and swipe-selection rectangle remain.
- [ ] Open editor pause/options and toggle **Show Ground** off and on.
- [ ] The valid selected ground reappears immediately.
- [ ] Select a community ground entry with no texture. The editor remains
      responsive and no half-built batch node is created.

Relevant log line for an unsupported texture:

```text
DYNARMIC_V22_NULL_BATCH_TEXTURE_REJECTED
```

## Legacy ARM music

- [ ] Test 1.0 and preferably 1.3/1.4.
- [ ] Compare attempt 1 with attempt 2 without moving the volume slider.
- [ ] Confirm there is no 100 ms muted prime or extra delay.

Expected first-level-play log:

```text
Audio legacy first-play MCI replay: second-start=ok
```

## Regression checks

- [ ] Practice Z/X remains Practice Mode-only.
- [ ] x86 behavior/pacing is unchanged.
- [ ] Network, backups and isolated saves remain unchanged.
