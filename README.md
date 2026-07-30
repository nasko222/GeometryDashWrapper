# Geometry Dash Wrapper 0.9.5 — EnduranceTest12

This source tree contains the unified Windows wrapper for legacy ARM, ARMv7
2.2 beta, and native x86 Geometry Dash APKs.

EnduranceTest12 is intentionally narrow:

- the **confirmed fixed** 2.2 editor black-region/background suppression from
  EnduranceTest10 is preserved unchanged;
- valid ground creation is restored, including the editor **Show Ground**
  toggle; a truly null texture now makes `CCSpriteBatchNode::initWithTexture`
  return false before it can create a frozen half-built node;
- the failed muted MCI prime is removed. Legacy ARM performs one immediate
  second-start command only on the first play of each newly opened level alias,
  without changing volume or stopping the decoder.

Practice Z/X, BPM guidelines, song lines, selection rectangles, networking,
version-isolated saves, native launch, and the accepted x86 pacing are retained.

Build with `BUILD_ALL.cmd`. APKs are intentionally not included.
