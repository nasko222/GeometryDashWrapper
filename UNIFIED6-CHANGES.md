# Unified6 test build changes

## What changed

### Comments
The WinHTTP bridge was already receiving the comment response and invoking the
callback. Unified6 changes the guest response byte-vector allocation from exact
`size` capacity to `size + 1`, writes a zero byte after the payload, and keeps
`end` at the true payload length. This preserves vector length while supporting
game code that reads the response as a C string. Comment endpoints also emit a
sanitized response preview in `gd-armv7.log`.

### World and Lite buttons
Unified5 only patched the menu entry and online capability check. World/Lite then
replaced selected CreatorLayer callbacks with `onOnlyFullVersion`. Unified6 finds
that exact native replacement block and branches over it, preserving the real
callbacks and the normal, enabled sprite tint. The signature was validated
against the supplied World ARMv7 library.

### Editor right-side black strip
Unified6 normalizes every cropped default-framebuffer viewport to the full client
area, rejects large persistent edge scissors, and resets viewport/scissor state
both before and after `nativeRender`. Render-to-texture framebuffers are not
changed.

### 2.11 high graphics
ARM now also patches `CCDirector::updateContentScale(TextureQuality)` so the
highest texture branch is selected, in addition to `isHD=true` and low-memory
checks returning false. This cannot create textures that are absent from the APK.

### Saves
All versions use one flat `save\` directory again. There are no package profiles,
profile migration, fingerprints, or package marker files.

### Launcher, title, and icon
The distribution launcher is `GeometryDash.exe`. It reads `GeometryDash.cfg` and
starts the private backend selected from the APK. The backend process modules are
stored under `backends\` with private `.dll` filenames. They retain executable PE
headers and are launched out-of-process; they are not loadable Windows DLLs.

The title detector now includes Geometry Dash Lite. Fixed per-game PNG/ICO files
are copied to `assets\icons`; icons are not decoded from the APK at startup. Both
Win32 window icons and class icons are updated.

## Required retest

1. Main 2.2 official-server account comments and level comments.
2. The same comments page on the GDPS.
3. Geometry Dash Lite Creator buttons: enabled artwork and working callbacks.
4. Geometry Dash World 1.0 gauntlets. This is a separate protocol/version test;
   the button patch is validated, but the server behavior still needs runtime
   confirmation.
5. Enter and leave the editor repeatedly and confirm the right strip never
   appears.
6. Geometry Dash 2.11 highest graphics.
7. Titles and both the top-left/taskbar icon for Dash, Lite, World, Meltdown, and
   SubZero.
8. Set `disable_windows_console = false`, launch once, then set it back to `true`.

Return `gd-armv7.log` from any failed ARMv7 test. For comments, keep the new
`ARMv7 comments response ABI` preview line in the log.
