# EnduranceTest2 Test Sheet

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

- [ ] x86 pacing remains as smooth as Fix6
- [ ] Cursor hides during x86 gameplay
- [ ] Cursor is visible throughout the pause menu
- [ ] Cursor hides again after returning to gameplay
- [ ] ARMv7 cursor hides during gameplay
- [ ] ARMv7 cursor remains visible in pause menu
- [ ] Top-right pause graphic is absent in selected 2.2 beta
- [ ] Escape opens the complete pause menu
- [ ] Resume/restart/quit buttons still work

## Selected 2.2 beta editor

- [ ] Sawblades use black texture
- [ ] Clubstep/Fingerdash monsters use black texture
- [ ] Editor playtest death X uses black texture
- [ ] Song-only vertical line renders and moves
- [ ] BPM guidelines render
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
