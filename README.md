# Geometry Dash Wrapper 0.9.5 — EnduranceTest11

This source tree contains the unified Windows wrapper for legacy ARM, ARMv7
2.2 beta, and native x86 Geometry Dash APKs.

EnduranceTest11 is intentionally narrow:

- the **confirmed fixed** 2.2 editor black-region/background suppression from
  EnduranceTest10 is preserved unchanged;
- a missing community ground texture is replaced at the real SpriteBatchNode
  construction boundary with packaged ground 1, preventing the null-texture
  half-object that froze EnduranceTest10;
- legacy ARM primes each newly opened non-looping music alias once, muted, then
  rewinds it before the actual first attempt. This tests whether the Windows MCI
  decoder's first-start state causes attempt 1 to be quieter than attempt 2+.

Practice Z/X, BPM guidelines, song lines, selection rectangles, networking,
version-isolated saves, native launch, and the accepted x86 pacing are retained.

Build with `BUILD_ALL.cmd`. APKs are intentionally not included.
