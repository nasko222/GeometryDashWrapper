# Geometry Dash Wrapper 0.9.5-unified6

Unified wrapper for x86 Android game libraries, legacy ARM through Dynarmic,
and ARMv7/2.2 through Dynarmic. There is no Unicorn backend.

## Running

1. Build with `BUILD_ALL.cmd` on Windows.
2. Put the APK at `dist-unified\game.apk`.
3. Edit `dist-unified\GeometryDash.cfg`.
4. Open `dist-unified\GeometryDash.exe`.

There is no launch BAT file. `GeometryDash.exe` reads the CFG, starts the Python
auto-selector without a console flash when configured, then launches the matching
private backend process module.

```ini
[launcher]
apk = game.apk
disable_windows_console = true
debug = false

[game]
gdps_server = www.boomlings.com/database
hack_icons = false
full_bypass = true
force_highest_graphics = true
music_pulse_max = 0.30
```

x86 is preferred when an x86 game library exists. Otherwise the launcher chooses
legacy ARM or ARMv7. The removed ARM override does not return.

## Unified6 changes

- Native HTTP response vectors reserve a trailing zero byte. This keeps valid
  `std::vector<char>` begin/end semantics while also supporting comment parsers
  that accidentally treat response data as a C string. Comment endpoints now log
  a short sanitized response preview for the next test.
- Geometry Dash World and Geometry Dash Lite preserve each Creator button's real
  callback and enabled artwork instead of swapping selected buttons to
  `onOnlyFullVersion`.
- The editor black-strip workaround is now deliberately aggressive: default
  framebuffer viewports are normalized to the full client area, large persistent
  edge scissors are rejected, and clip state is reset both before and after each
  rendered frame.
- ARM now patches `CCDirector::updateContentScale(TextureQuality)` in addition to
  the HD and low-memory checks. This is the final wrapper-side 2.11 high-texture
  attempt; an APK that contains no larger textures cannot be upgraded by a flag.
- Saves are flat again under `save\`. No package profiles, migration, or
  `.last-package` marker are created.
- Titles include Geometry Dash Lite in addition to Geometry Dash, World,
  Meltdown, and SubZero.
- Icons are fixed files under `assets\icons`, not extracted from the APK at
  startup. Each ICO contains dedicated 16/20/24/32/40/48/64/128/256px entries,
  and both the window and window class icons are updated.
- Distribution layout is `GeometryDash.exe`, `GeometryDash.cfg`, `run_auto.py`,
  `assets\icons`, and `backends\...\*.dll`. The backend files are executable PE
  process modules with private DLL filenames so one x64 launcher can start both
  32-bit and 64-bit backends without loading them into the same process.

The working 1.6 x86 network bridge, platformer controls, swing behavior, and icon
unlock setting are retained.
