# Unified7 Fix2 — focused regression fixes

- ARMv7 uses only the companion `ShaderFix` hook for the solid-white black-mask
  objects (sawblades, editor death X, Clubstep/Fingerdash monsters).
- Editor viewport/scissor values pass through exactly again so the narrow song
  line is not expanded away. The first editor default-framebuffer color clear
  becomes a full clear to remove the persistent moving right-edge region.
- Lite builds with gauntlet code but no GauntletSheet assets receive a safe
  no-op guard on only the gauntlet callback instead of crashing.
- Exact GD 1.0.0 legacy ARM (`libgame.so` 5551628 bytes, CRC32 60fa62f4)
  skips the forced-HD hook that caused its nativeInit CCSet/vtable exception.
- x86 port-80 API sockets are synthetic-ready for the existing WinHTTP bridge,
  preventing 2.11 Boomlings login from stalling before the request is sent.
- `getAccountURL.php` HTTPS results are presented to old 2.11 as HTTP while the
  wrapper still performs network transport through WinHTTP, avoiding the guest
  OpenSSL crash before account sync.
- SubZero's real icon fills the Windows icon canvas better. Meltdown/other APK
  icon lookup now searches all icon families, and package identity takes
  precedence over bundled spin-off level-data files.
- 2.11 highest-graphics behavior and logged-in comments are intentionally
  unchanged.

Windows runtime testing is required. After the TLS crash is removed, genuinely
incompatible 2.2 cloud data may reveal a separate parser incompatibility.
