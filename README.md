# Geometry Dash Wrapper 0.9.5 — EnduranceTest9

`endurancetest` is the cross-version stability branch. EnduranceTest9 is a
focused correction based on `logs(10).zip` after BPM guidelines were confirmed
working in EnduranceTest8.

## Changes from EnduranceTest8

- anchors the selected 2.2 beta editor's three real background nodes at their
  initial positions, while leaving gameplay background callsites untouched;
- replaces the fragile nested guest `CCSpriteBatchNode::updateBlendFunc()` call
  with an exact host reproduction of its four field accesses;
- makes community background/ground entries with absent texture data fail
  safely with standard alpha blending instead of crashing or freezing;
- keeps the confirmed BPM setup path, selection rectangle, object lifecycle,
  Z/X controls, x86 pacing, networking, backups and save isolation unchanged;
- fully removes the failed legacy first-play volume-1000 experiment and adds no
  new unverified audio behavior.

Nothing in this source archive is claimed Windows-runtime-confirmed until tested
with the target APKs. See `ENDURANCETEST9.md` and
`ENDURANCETEST9-VERIFICATION.txt`.
