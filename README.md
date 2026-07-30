# Geometry Dash Wrapper 0.9.5 — EnduranceTest8

`endurancetest` is the cross-version stability branch. EnduranceTest8 is a
focused 2.2 beta editor correction based on `logs(9).zip` and the screenshot of
the moving black region.

## Changes from EnduranceTest7

- freezes only the selected beta editor's background-art scrolling call;
- rebuilds BPM markers through `LevelEditorLayer::levelSettingsUpdated()` once
  per editor session;
- skips the exact null `CCSpriteBatchNode::updateBlendFunc()` call responsible
  for the repeatable ground-texture crash, while preserving all valid calls;
- enables a legacy-only first-level-play volume-1000 experiment so attempt 1
  targets the louder attempt-2 level;
- keeps Z/X, x86 pacing, networking, backups, save isolation, selection
  rectangle rendering and editor camera/object lifecycle unchanged.

Nothing in this source archive is claimed Windows-runtime-confirmed until tested
with the target APKs. See `ENDURANCETEST8.md` and
`ENDURANCETEST8-VERIFICATION.txt`.
