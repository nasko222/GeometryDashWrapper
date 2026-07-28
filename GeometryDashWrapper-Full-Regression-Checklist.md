# Geometry Dash Wrapper — Full Regression Checklist

Test everything on **`0.9.5-unified7-recovery`**. The wrapper version beside each item shows **where the bug originally appeared**, not which build you should run now.

**Status key:**  
🔴 last reported broken · 🟡 fix added but never successfully tested · 🟢 previously working, check for regression · ⚪ unknown

I excluded compiler-only bring-up failures that cannot be tested through gameplay.

---

## 1. Test these blockers first

- [ ] **B01 — Unified6 launcher regression**  
  **Origin:** `0.9.5-unified6` · **GD:** Every version  
  `RUN_AUTO.cmd` must launch the game. No `GeometryDash.exe`, `.cfg`, or backend DLL arrangement should be required.  
  **Last state:** 🟡 reverted in Unified7.

- [ ] **B02 — x86 backend selection**  
  **Origin:** Unified1–6 · **GD:** 1.5–2.11  
  Must select `x86\GeometryDashWrapper.exe`.

- [ ] **B03 — old ARM backend selection**  
  **Origin:** Unified1–6, based on `dynarmictest14-fix1` · **GD:** 1.0–1.4  
  Must select `arm-legacy\GeometryDashArmLegacy.exe`.

- [ ] **B04 — ARMv7 backend selection**  
  **Origin:** Unified1–6, based on `0.9.4-milestone1` · **GD:** 2.2 beta/SubZero ARMv7  
  Must select `armv7\GeometryDashArmV7.exe`.

- [ ] **B05 — comments on main servers**  
  **Origin:** Unified5–7 · **GD:** 2.2 beta  
  Open a user profile and comments. Comments must appear and the loading circle must stop.  
  The latest log proves Boomlings returned HTTP 200, a valid 475-byte body, and the callback ran; the hang is after delivery.  
  **Last state:** 🔴 broken; 🟡 Unified7 fix untested.

- [ ] **B06 — comments on GDPS**  
  **Origin:** Unified5–7 · **GD:** 2.2 beta  
  Repeat B05 with the configured GDPS.  
  **Last state:** 🔴 broken; 🟡 fix untested.

- [ ] **B07 — editor black moving region**  
  **Origin:** 2.2 bringups through Unified7 · **GD:** 2.2 beta editor  
  Start playtest, let the icon/camera move right for some time, wait for black area from the right, then stop. It must disappear immediately and not continue moving right-to-left.  
  **Last state:** 🔴 broken; 🟡 viewport/scissor fix untested.

- [ ] **B08 — editor song timeline line**  
  **Origin:** Unified6 report · **GD:** 2.2 beta editor  
  Play only the song in editor. The vertical progress line must render and move.  
  **Last state:** 🔴 broken; 🟡 fix untested.

- [ ] **B09 — Lite full-version buttons**  
  **Origin:** Unified5–7 · **GD:** attached Lite APK  
  Test every formerly disabled button. They must open the real page, not show “unlock at full version.”  
  **Last state:** 🔴 broken; 🟡 APK-specific Unified7 patch untested.

- [ ] **B10 — World buttons**  
  **Origin:** Unified4–6 · **GD:** World 1.0 and 1.0.2  
  Buttons must be visible, clickable, and call their original actions.  
  **Last state:** World-only fix previously worked, but broad patches regressed it.

- [ ] **B11 — correct icons and titles**  
  **Origin:** Unified5–7 · **GD:** Dash, Lite, World, Meltdown, SubZero  
  Test taskbar icon, small top-left icon, and window title for all five games. Icons must be real game icons, not painted substitutes.

- [ ] **B12 — one local save folder**  
  **Origin:** Unified builds · **GD:** all  
  Saves must use the normal local `save\` folder, with no separate “compatible saves” folders.

- [ ] **B13 — 2.11 highest graphics**  
  **Origin:** Unified5–7 · **GD:** 2.11  
  Compare off/on carefully. Textures should become sharper/larger when the APK actually contains HD assets.  
  **Last state:** 🔴 no visible effect; possibly an APK limitation.

- [ ] **B14 — World gauntlets**  
  **Origin:** Unified5 · **GD:** World  
  World 1.0 previously loaded no gauntlets; 1.0.2 loaded all. Test both separately.

- [ ] **B15 — network request must never freeze the window**  
  **Origin:** NetworkTest1–3 · **GD:** old ARM and ARMv7  
  Use a dead/slow server or disconnect internet during a request. UI must remain responsive and eventually time out.  
  **Last state:** 🟢 previously fixed; critical regression check.

---

# 2. Packaging, launcher, saves, and UI

- [ ] **PKG01 — Geometry Dash 1.6 prefers x86**  
  **Origin:** unified auto-selection · **GD:** 1.6  
  Since 1.6 variants can contain ARM/x86, x86 must win when available.

- [ ] **PKG02 — ARM override removed**  
  **Origin:** Unified BAT configuration  
  `OVERRIDE_ARM` must be completely absent and must not affect selection.

- [ ] **PKG03 — all three backend EXEs are packaged**  
  Confirm x86, old ARM and ARMv7 executables exist in their expected folders.

- [ ] **PKG04 — no stale Unified6 layout**  
  Old `GeometryDash.exe`, `GeometryDash.cfg`, or `backends\*.dll` files must not be accidentally launched.

- [ ] **PKG05 — cleanup does not delete APK/source**  
  **Origin:** `dynarmictest4`  
  Rebuilding or cleaning must not delete the game APK or dependent source files.

- [ ] **PKG06 — source ZIP contains no APKs**  
  The source archive must remain reasonably sized and contain no bundled APKs or proprietary game libraries.

- [ ] **PKG07 — normal in-game exit**  
  **Origin:** DynarmicTest7/8  
  Exit button must close the window and terminate the process promptly.

- [ ] **PKG08 — Windows close button**  
  Closing with the title-bar X must terminate just as cleanly.

- [ ] **UI01 — full game title:** `Geometry Dash`
- [ ] **UI02 — Lite title:** `Geometry Dash Lite`
- [ ] **UI03 — World title:** `Geometry Dash World`
- [ ] **UI04 — Meltdown title:** `Geometry Dash Meltdown`
- [ ] **UI05 — SubZero title:** `Geometry Dash SubZero`

- [ ] **UI06 — icon switches between games**  
  Launch Lite, close it, then World, SubZero, Meltdown and full Dash. No stale icon/title from the previous launch.

- [ ] **SAVE01 — saves do not escape to `D:\data\data\...`**  
  **Origin:** Bringup16/17 · **GD:** selected beta, 2019 beta, stock SubZero  
  `CCGameManager.dat`, `CCLocalLevels.dat` and backups must remain local.

- [ ] **SAVE02 — existing misplaced-save recovery**  
  Recovery must not overwrite a newer local save.

- [ ] **SAVE03 — local level progress persists**  
  Complete or partially complete a level, exit, reopen, verify progress.

- [ ] **SAVE04 — settings persist**  
  Change audio/graphics/settings, restart and verify.

- [ ] **SAVE05 — created levels persist**  
  Create a level, save, restart, verify it remains in My Levels.

- [ ] **SAVE06 — stars and coins survive GDPS save/load**  
  **Origin:** Unified5 · **GD:** 2.2 beta  
  Save an account with known star/coin counts, alter local state, load and compare.  
  **Last state:** 🔴 possibly broken or GDPS-specific.

- [ ] **SAVE07 — empty/corrupt game-manager recovery**  
  **Origin:** Bootstrap13/14  
  An empty or damaged file must not permanently break startup; backup recovery should work.

- [ ] **SAVE08 — attempts and level contents after reload**  
  **Origin:** bootstrap era  
  Attempt number must be correct and levels must not become empty after first load.

- [ ] **SAVE09 — 2.2 save opened by 2.11**  
  **Origin:** Unified5  
  Must not hard-crash. Ideally reject incompatible data cleanly.  
  **Last state:** 🔴 crash reported.

- [ ] **SAVE10 — Boomlings load error `-11`**  
  **Origin:** Unified3/4 · **GD:** beta client loading modern 2.2 release data  
  Record whether this remains a client-version incompatibility rather than a network failure.

- [ ] **SAVE11 — same-version GDPS account save/load**  
  Previously worked in Unified3; verify no regression.

---

# 3. x86 backend: Geometry Dash 1.5–2.11

- [ ] **X8601 — launch every major x86 version**  
  Test 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.1 and 2.11 menus and one level each.

- [ ] **X8602 — 1.6 main-server networking**  
  Online list, level download and any available comments/profile functions.  
  **Last state:** 🟢 “works amazing.”

- [ ] **X8603 — 1.6 configured GDPS networking**  
  Browse/download/upload a tiny test level.

- [ ] **X8604 — 2.11 highest-graphics comparison**  
  Test with both settings and take identical screenshots for comparison.

- [ ] **X8605 — icon hack**  
  **Last state:** 🟢 reported fine.

- [ ] **X8606 — custom songs**  
  Search song, download and play it.

- [ ] **X8607 — social/external links**  
  Must open the default Windows browser.

- [ ] **X8608 — save compatibility failure handling**  
  Invalid or newer save data must not crash the wrapper process.

---

# 4. Old ARM backend: Geometry Dash 1.0–1.4

## Basic compatibility

- [ ] **ARM01 — Geometry Dash 1.0 launch**
- [ ] **ARM02 — Geometry Dash 1.1 launch**
- [ ] **ARM03 — Geometry Dash 1.2 launch**
- [ ] **ARM04 — Geometry Dash 1.3 launch**
- [ ] **ARM05 — Geometry Dash 1.4 launch**

For each: menu, settings, official level, pause, restart, exit and relaunch.

## Historical rendering and level bugs

- [ ] **ARM06 — continuous rendering**  
  **Origin:** Bootstrap1–6  
  No white screen, one-frame loading screen, or instant crash.

- [ ] **ARM07 — particles**  
  **Origin:** bootstrap era  
  Jump, trail, death and completion particles render.

- [ ] **ARM08 — Clutterfunk full level**  
  **Origin:** Bootstrap7 · **GD:** 1.4  
  Previously had FPS drop and level cut off early.

- [ ] **ARM09 — xStep contents**  
  **Origin:** Bootstrap7 · **GD:** 1.4  
  Previously loaded as an empty level.

- [ ] **ARM10 — editor object vertical position**  
  **Origin:** Bootstrap7  
  Objects previously appeared sunk into the ground.

- [ ] **ARM11 — editor-created level data persists**  
  **Origin:** Bootstrap7  
  New editor levels previously lost data after restart.

- [ ] **ARM12 — attempt label**  
  **Origin:** Bootstrap7  
  Must display normal attempt text, not strange Cocos/internal text.

- [ ] **ARM13 — level remains populated after repeat load**  
  A level must not become empty after loading it once.

- [ ] **ARM14 — editor opens and functions**  
  Place, delete, save, playtest and reopen objects.

## Old ARM networking

- [ ] **ARM15 — online level browsing**
- [ ] **ARM16 — online level download**
- [ ] **ARM17 — GDPS level upload**
- [ ] **ARM18 — GDPS level download**
- [ ] **ARM19 — browser/social links**

Internet was completely broken in DynarmicTest8 and later restored; test all five.

## Old ARM audio/input/lifecycle

- [ ] **ARM20 — menu music**
- [ ] **ARM21 — level music**
- [ ] **ARM22 — jump/death/click effects**
- [ ] **ARM23 — approximately 250 ms audio delay**  
  **Origin:** Bootstrap7.

- [ ] **ARM24 — first/death SFX frame spike**  
  **Origin:** later Dynarmic builds  
  First effect must not cause an approximately 250 ms gameplay hitch.

- [ ] **ARM25 — first cached music desynchronization**  
  Compare first playback after clean cache and second playback.

- [ ] **ARM26 — keyboard first-open delay**  
  Previously paused roughly 1–2 seconds.

- [ ] **ARM27 — Space in editor name/description**  
  **Origin:** DynarmicTest7  
  Space must type normally and not trigger gameplay.  
  **Last state:** 🟢 previously fixed.

- [ ] **ARM28 — exit button/process cleanup**  
  **Last state:** 🟢 previously fixed.

## Old ARM performance

- [ ] **ARM29 — first level load time**  
  Previously took roughly 5–10 seconds. Record cold and warm loads.

- [ ] **ARM30 — first menu-pane load time**
- [ ] **ARM31 — end cutscene FPS**  
  Previously could drop to around 5–10 FPS.

- [ ] **ARM32 — RTX 4060 baseline**  
  Mostly 60 FPS expected.

- [ ] **ARM33 — GTX 1050-class PC**  
  Test Stereo Madness, Xstep, Clutterfunk and editor. Earlier Test7 report had severe lag/level-start failures; later friend report said “mega smooth.”

- [ ] **ARM34 — Intel HD 2500**  
  Previously unplayable; record current result even if still below 60 FPS.

---

# 5. ARMv7 / 2.2 beta compatibility

## APK startup matrix

- [ ] **V2201 — 141–147 MB selected 2022/editor APK launches**
- [ ] **V2202 — 95–97 MB 2019 beta/platformer APK launches**
- [ ] **V2203 — vanilla SubZero launches**
- [ ] **V2204 — World launches through correct backend**
- [ ] **V2205 — Lite launches through correct backend**
- [ ] **V2206 — Meltdown launches through correct backend**

For each: menu, one level, settings, online page and exit.

## 2.2 networking

- [ ] **V2207 — main-server level list**
- [ ] **V2208 — main-server level download**
- [ ] **V2209 — configured GDPS list**
- [ ] **V2210 — configured GDPS upload/download**
- [ ] **V2211 — account search**
- [ ] **V2212 — profile information**
- [ ] **V2213 — Boomlings comments**
- [ ] **V2214 — GDPS comments**
- [ ] **V2215 — login**
- [ ] **V2216 — account save**
- [ ] **V2217 — account load**
- [ ] **V2218 — custom song metadata**
- [ ] **V2219 — custom song download**
- [ ] **V2220 — daily/weekly level**
- [ ] **V2221 — network timeout without freeze**
- [ ] **V2222 — comments empty/error response stops spinner**
- [ ] **V2223 — comments appear without clicking again after response**

## Editor entry

- [ ] **V2224 — My Levels editor entry**  
  Selected APK.

- [ ] **V2225 — wrench/Edit button from level page**  
  Initially did nothing; later faded and crashed.

- [ ] **V2226 — pause-menu Edit button**  
  Last direct report said it still did not work.

- [ ] **V2227 — end-screen Edit button**
- [ ] **V2228 — F2 remains removed**
- [ ] **V2229 — selected APK editor uses compatible `libgame.so`**
- [ ] **V2230 — 2019 beta editor**  
  **Last state:** 🔴 did not open.

- [ ] **V2231 — vanilla SubZero editor**  
  **Last state:** 🔴 did not open.

- [ ] **V2232 — APK without editor remains stable**  
  Even when editor is unsupported, normal gameplay must not crash.

## Editor rendering and stability

- [ ] **V2233 — placed objects render in editor**  
  Previously invisible but present in playtest.

- [ ] **V2234 — placed objects render in playtest**
- [ ] **V2235 — empty editor cube/player is not stuck**
- [ ] **V2236 — editor remains responsive for 10 minutes**  
  Previously froze after some time.

- [ ] **V2237 — pan/zoom/place/delete stress test**
- [ ] **V2238 — black strip exact reproduction**
- [ ] **V2239 — black strip remains gone after stopping**
- [ ] **V2240 — black strip remains gone after pan/zoom**
- [ ] **V2241 — song-only vertical line**
- [ ] **V2242 — stopping song clears timeline/render state**
- [ ] **V2243 — color picker and editor overlays still render**  
  Ensures the strip fix did not break legitimate framebuffers.

- [ ] **V2244 — editor text fields accept spaces**
- [ ] **V2245 — first keyboard opening has no long delay**

## Specific level crashes

- [ ] **V2246 — Knock Em Out**  
  Parser-null/crash history.  
  **Last detailed state:** 🔴 broken.

- [ ] **V2247 — Press Start**  
  **Last detailed state:** 🔴 broken.

- [ ] **V2248 — Fingerdash**  
  **Last detailed state:** 🔴 broken.

- [ ] **V2249 — Power Trip**  
  **Last state:** 🟢 worked in Bringup9.

- [ ] **V2250 — at least ten other official levels**  
  The old pattern was “some levels crash, some do not.”

## Platformer and swing

- [ ] **V2251 — A/D movement**
- [ ] **V2252 — Left/Right arrows**
- [ ] **V2253 — Space jump**
- [ ] **V2254 — Up-arrow jump**
- [ ] **V2255 — left-mouse jump**
- [ ] **V2256 — on-screen buttons visible**
- [ ] **V2257 — on-screen buttons clickable**
- [ ] **V2258 — pressing buttons does not permanently disable LMB jump**
- [ ] **V2259 — editor-playtest platformer movement**
- [ ] **V2260 — editor-playtest does not freeze/crash**
- [ ] **V2261 — input ownership releases correctly**
- [ ] **V2262 — swing gameplay**
- [ ] **V2263 — platformer/swing controls survive leaving and reopening menu**

Platformer was eventually reported fixed and swing “works amazing,” so these are regression tests.

## 2.2 audio

- [ ] **V2264 — menu music**
- [ ] **V2265 — official level music**
- [ ] **V2266 — UI click effects**
- [ ] **V2267 — jump/death effects**  
  Bringup12/13 had music but no SFX.

- [ ] **V2268 — first cached music synchronization**
- [ ] **V2269 — pause/resume audio**
- [ ] **V2270 — minimize/restore audio**

## Extension libraries

- [ ] **V2271 — validated `libgame.so` editor functionality**
- [ ] **V2272 — no blind execution of every `.so`**
- [ ] **V2273 — `CollisionFix` causes no regression**
- [ ] **V2274 — `ShaderFix` causes no regression**
- [ ] **V2275 — no crash from `libhooking`, Dobby or incompatible `libgdkit`**
- [ ] **V2276 — normal game still works when companion hooks are unavailable**

---

# 6. World, Lite, Meltdown and SubZero-specific checks

- [ ] **GAME01 — World 1.0 gauntlets**
- [ ] **GAME02 — World 1.0.2 gauntlets**
- [ ] **GAME03 — World formerly locked buttons**
- [ ] **GAME04 — World buttons remain clickable after reopening menu**
- [ ] **GAME05 — Lite formerly locked buttons**
- [ ] **GAME06 — Lite buttons open correct pages, not merely lose the dark tint**
- [ ] **GAME07 — full-version bypass does not break Back/Play/Settings/Online**
- [ ] **GAME08 — vanilla SubZero official levels**
- [ ] **GAME09 — vanilla SubZero online functions**
- [ ] **GAME10 — Meltdown official levels**
- [ ] **GAME11 — each game gets its own correct title**
- [ ] **GAME12 — each game gets correct taskbar and small icon**

---

## Efficient testing order

1. **B01–B15** first. These determine whether the build is usable.
2. Test **V22-SELECTED** completely, because it exposes comments, editor, black strip, song line, platformer and companion-library bugs.
3. Test **Lite**, then **World 1.0 and 1.0.2**.
4. Test old ARM **1.0 and 1.4** first; if both work, continue 1.1–1.3.
5. Finish with **1.6 x86**, **2.11 graphics**, GTX 1050 and Intel HD 2500.

Send failed items back in this format:

```text
TEST ID:
Wrapper build:
APK / GD version:
PASS / FAIL / PARTIAL:
Exact steps:
What happened:
Log:
Screenshot/video:
```

For **B07/V2238**, note approximately how many seconds the playtest ran before the black area appeared.
