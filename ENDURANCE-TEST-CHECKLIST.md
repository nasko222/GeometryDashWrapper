# EnduranceTest4 Test Sheet

Copy this file and mark results.

## Core launch matrix

- [ ] GD 1.0 legacy ARM
- [ ] GD 1.01 legacy ARM
- [ ] GD 1.4 legacy ARM
- [ ] GD 1.6 x86
- [ ] GD 1.8 x86
- [ ] GD 1.9 x86
- [ ] GD 1.93 x86
- [ ] GD 2.0 x86
- [ ] GD 2.1 x86
- [ ] GD 2.11 x86
- [ ] GD Lite
- [ ] GD World
- [ ] GD Meltdown
- [ ] GD SubZero
- [ ] Selected GD 2.2 beta

## Cross-version saves

- [ ] Every run reports the expected package/version/backend save folder
- [ ] 1.93 starts repeatedly with its own profile
- [ ] 2.0 progress survives restart
- [ ] 2.11 progress survives restart
- [ ] No version reads another version's `CCGameManager.dat`
- [ ] `VERSION_ISOLATED_SAVES=false` still selects the flat save root

## Network/account

- [ ] 1.93 login
- [ ] 1.93 backup
- [ ] 1.93 sync/load
- [ ] 2.0 backup
- [ ] 2.1 online levels
- [ ] 2.11 online levels
- [ ] Boomlings launcher
- [ ] GDPS launcher
- [ ] No network request freezes the window

For a failed backup, search the run log for:

```text
Network HTTP interim response
Network HTTP request pending body
Network HTTP pending body joined
backupGJAccount
Network legacy API completed through WinHTTP
```

## Desktop gameplay behavior

- [ ] x86 pacing remains as smooth as EnduranceTest3
- [ ] Cursor stays visible during x86 gameplay, pause and Resume
- [ ] Cursor stays visible during legacy ARM gameplay, pause and Resume
- [ ] Cursor stays visible during ARMv7 gameplay, pause and editor use
- [ ] Native top-right pause button remains present in every version
- [ ] Escape opens the complete pause menu
- [ ] Resume/restart/quit buttons still work
- [ ] Practice Mode: Z places a checkpoint
- [ ] Practice Mode: X removes the latest checkpoint
- [ ] Normal Mode: Z/X do not create or remove checkpoints
- [ ] Holding Z/X does not auto-repeat checkpoint actions
- [ ] 1.0–1.4 music volume is identical on attempt 1, attempt 2 and later

## Selected 2.2 beta editor

- [ ] Sawblades use black texture
- [ ] Clubstep/Fingerdash monsters use black texture
- [ ] Record the editor playtest death-X colour (unchanged in this build)
- [ ] Song-only vertical line renders and moves on first editor open
- [ ] BPM guidelines render on first editor open
- [ ] Close/reopen editor: song line renders and moves again
- [ ] Close/reopen editor: BPM guidelines render again
- [ ] No moving black region appears at the right
- [ ] Stopping playtest removes stale clip/black region
- [ ] Pan and zoom remain correct
- [ ] Objects render in editor and playtest
- [ ] Editor opens without the exact-visibility freeze
- [ ] Editor remains usable for 20 minutes
- [ ] Pause/resume works during editor playtest

## Result template

```text
Test:
APK/version:
Backend:
Server:
Duration:
PASS / FAIL / PARTIAL:
Exact steps:
Observed result:
Run folder:
```
