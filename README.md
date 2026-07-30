# Geometry Dash Wrapper 0.9.5 — EnduranceTest10

This source tree contains the unified Windows wrapper for legacy ARM, ARMv7
2.2 beta, and native x86 Geometry Dash APKs.

EnduranceTest10 is a narrow 2.2 editor correction. It clamps community-expanded
background/ground selectors to assets actually packaged by the selected beta and
suppresses all three background-scrolling callsites only while the editor is
active. Normal gameplay backgrounds remain on the game's original path.

Confirmed working behavior retained from earlier builds includes Practice Z/X,
BPM guidelines, selection rectangles, online transport, isolated saves, native
launch, dated logs, and the accepted x86 frame pacing.

Build with `BUILD_ALL.cmd`. APKs are intentionally not included.
