# 0.9.6-gdpsfixes2

Branch: `gdpsfixes2`

## Color picker crash diagnosis

Both supplied Geometry Dash 1.0 and 1.01 logs fail on the same missing resource:

`/data/data/com.robtopx.geometryjump/extensions/CCControlColourPickerSpriteSheet.plist`

Immediately after that failed read, both builds enter
`cocos2d::CCSpriteBatchNode::updateBlendFunc()` with a null object and fault at
guest address `0x54`. The APK resource cache proves the corresponding color
picker sprite sheet exists under `assets/` in resolution-specific `-hd` form.

## Fix

The legacy ARM and ARMv7 file bridges now provide a narrowly-scoped fallback
for read-only `extensions/...` resources:

1. Keep the normal translated writable-file lookup first.
2. If it is missing, strip only the `extensions/` prefix.
3. Try the exact APK asset basename.
4. If absent, try the old cocos2d `-hd` resolution suffix before the extension.
5. Serve the matched APK member through the existing memory-backed guest FILE
   implementation, including fread/fseek/ftell/fclose behavior.

This is intentionally not a generic missing-file redirect, and write/save paths
are never affected.

## Carried from gdpsfixes1

- >4095-byte ARM formatted-string / GDPS level-upload truncation fix.
- Stop-before-seek MCI behavior for retry/StartPos compatibility.
- Legacy ARM nonblocking connect/recv behavior for bad-network freezes.
- Android-only branch; experimental iOS backend remains removed.

## Still pending

- Editor WASD/Q transform shortcuts need a verified editor callback ABI.
- Cursor hiding and pause-button removal remain intentionally excluded.
