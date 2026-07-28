# Unified7 Fix5 stabilization

This is a narrow regression/stability release.

## Runtime changes

1. Saves are selected by package + version + backend. No automatic migration
   from the old flat save root is performed.
2. ARMv7 shader text is passed to OpenGL exactly as supplied by the guest.
3. x86 2.11 uses real nonblocking API connects; older x86 clients use the
   synthetic-ready WinHTTP bridge.
4. x86 no longer sleeps after a successful vsynced buffer swap.
5. The gameplay pause button is disabled when the matching `UILayer::onPause`
   export exists. The setting can be disabled in either launcher BAT.
6. The x86 mouse cursor is hidden only while a live `PlayLayer` is detected,
   and returns for text input, focus loss, Escape, menus and level exit.
7. Window titles are game names only.

## Intentionally unchanged

- Icons. User-supplied PNGs will be handled separately.
- Performance/JIT optimization beyond the x86 frame-pacing correction.
- Cross-version cloud-save parsing.
- Official-server features removed or unsupported by old clients.
