# Geometry Dash Wrapper 0.9.6-gdpstweaks7

Focused correction over gdpstweaks6.

## 2.2 beta stock editor restoration

The wrapper-owned LevelEditorLayer initializer now calls the stock APK's own
`GJBaseGameLayer::init()` before applying editor-specific state. The working 2023
editor mod does this first; tweaks5/6 omitted it, leaving all stock 2.2 beta
profiles with partially uninitialized base-layer state before `setupLayers()` and
`createObjectsFromSetup()`.

Exact stock profiles remain:
- 2019: 9,144,004-byte ARMv7 core
- 2022: 9,541,500-byte ARMv7 core
- 2023/SubZero: 9,578,364-byte ARMv7 core

## Pause button / cursor defaults

`REMOVE_PAUSE_BUTTON=true` and `HIDE_CURSOR_WHEN_PLAYING=true` are now defaults
in both launch CMD files, shared settings, and launcher-generated runtime config.
They remain toggleable by setting either variable to `false`.

Pause suppression is applied before each guest render. x86 and legacy ARM also
reassert `setVisible(false)` instead of treating a PlayLayer as permanently
handled after one hide. This is an intentionally aggressive attempt to prevent
the desktop pause control from ever reaching the presented frame. Escape pause
remains unchanged.

## Runtime status

Static/source validation only in this environment. Windows gameplay testing is
required, especially stock 2019/2022/2023 editor entry and the new pre-render
pause suppression behavior.
