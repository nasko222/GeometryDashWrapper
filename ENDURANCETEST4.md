# EnduranceTest4

Focused rollback and editor-state repair over EnduranceTest3.

## Changes

- Removed every wrapper feature that hides the Windows mouse cursor. The cursor is now left to Windows/the game on x86, legacy ARM and ARMv7.
- Removed every wrapper feature that suppresses or hides the top-right pause button. The APK's native pause control and Escape pause path are untouched.
- Added desktop Practice Mode controls: **Z** calls `UILayer::onCheck` and **X** calls `UILayer::onDeleteCheck`, matching the original PC controls. Auto-repeat is ignored.
- Reapplies the stored MCI music volume after play, resume and seek/replay operations to target the legacy ARM “first attempt quiet, later attempts louder” bug.
- Resets the 2.2 editor song/BPM overlay session counters whenever a new editor layer is opened, so the guide refresh starts from frame 1 each time.
- Adds an editor-only OpenGL scissor/viewport sanitizer before rendering. This targets the stale right-edge clipping state that grows into the moving black region after editor playtest.
- Keeps the accepted EnduranceTest3 x86 pacing and network/save transport unchanged. No x86 optimisation experiment is included.

## Runtime status

Source and syntax checks pass in the build environment available here. Windows/APK runtime confirmation is still required for the black-region repair, repeated editor reopening, legacy music volume and Practice Mode callbacks.
