# EnduranceTest11

Focused follow-up to the EnduranceTest10 Windows logs.

## 2.2 beta invalid ground

EnduranceTest10 correctly clamped the original GameManager limits, but the
community selector can still provide an entry that resolves to no texture. The
uploaded run reaches `CCSpriteBatchNode::updateBlendFunc()` with
`atlas->texture == nullptr` and stops immediately afterward. Merely supplying
blend factors leaves `initWithTexture()` operating on a half-built object.

EnduranceTest11 uses the exact beta texture-cache and texture-atlas APIs to load
packaged `groundSquare_01_001.png`, installs it into that atlas, then lets the
constructor continue with a valid texture. This path is editor-only and is used
only when the selected ground texture is missing.

## Legacy first-attempt music

The legacy log shows no wrapper volume command between attempt 1 and attempt 2.
Each later attempt replays the same open MCI alias. This build therefore does
not add another persistent volume override. Instead, only legacy ARM performs a
one-time muted 100 ms decoder prime on each newly opened non-looping music
alias, stops and rewinds it, restores its MCI volume, and then starts the real
attempt normally. Menu loops are excluded.

## Preserved unchanged

- confirmed 2.2 editor black-region fix;
- BPM guidelines and per-frame song guide;
- selection rectangle/client-array correction and editor culling;
- Practice Z/X;
- complete x86 backend and accepted pacing;
- networking, backups, native launch, and isolated saves.

Both new behaviors require a Windows test before they can be called confirmed.
