# Geometry Dash Wrapper 0.9.5 — EnduranceTest6

`endurancetest` remains the cross-version stability branch. EnduranceTest6 is
primarily a 2.2-beta editor rendering repair. It replaces the inaccurate host
visibility approximation, repairs guest client-array drawing, and stops
periodically clearing BPM markers.

## Running

Python is not required.

- `RUN_AUTO_GDPS.cmd` uses `naskogdps17.7m.pl/database`.
- `RUN_AUTO_BOOMLINGS.cmd` uses `www.boomlings.com/database`.

Double-click a launcher to use `game.apk`, or drag an APK onto either launcher.
Version-isolated saves remain enabled by default.

## 2.2 beta editor rendering

- The host visibility bridge now follows the beta companion's real camera
  rectangle and section lifecycle. Objects entering view are activated; objects
  leaving view are sent through `GameObject::deactivateObject(false)`. The old
  bridge forced every encountered object visible forever and never deactivated
  anything, which could leave stale batches after panning or playtesting.
- The bridge keeps the beta's `preUpdateVisibility`, colour queue,
  `processAreaVisualActions`, and batch sorting order, but computes culling in
  host code so it does not reproduce EnduranceTest1's multi-second exact-helper
  freeze.
- Mapped guest pointers passed to `glVertexAttribPointer` and `glDrawElements`
  are now treated as client memory even if a stale VBO binding is cached. The
  wrapper temporarily unbinds the actual buffer for that call and restores it.
  This directly targets the swipe-selection rectangle and other thin line
  primitives that use stack vertex arrays.
- `DrawGridLayer::updateTimeMarkers()` is no longer called every 60 frames. It
  runs only when the beta's non-empty marker source string changes, preventing
  a transient empty source from erasing BPM guidelines.
- The song-position line remains updated every rendered editor frame.
- `LevelEditorLayer::updateGridLayer` remains completely untouched by the host.
- The failed EnduranceTest4 viewport/scissor reset remains removed.

## Older x86 Practice Mode controls

Older x86 binaries use `UILayer::onCheck()` and `onDeleteCheck()` without a
sender argument. EnduranceTest6 resolves both that ABI and the newer
`CCObject*` ABI. The existing fail-closed Practice Mode guard remains in place,
so Z/X cannot place checkpoints in Normal Mode.

## Legacy music

The EnduranceTest5 45-frame MCI volume reassert loop is removed because the new
logs prove it did not change the attempt-to-attempt volume behaviour. Immediate
volume application after play/resume/seek remains. No replacement audio theory
is claimed fixed in this render-focused build.

## Deliberately unchanged

- x86 pacing and optimisation code;
- networking, backup transport and save isolation;
- native mouse visibility and native pause-button behaviour;
- editor clip state outside the targeted client-array bridge.

## Runtime confirmation needed

The 2.2 editor changes are derived from the exact beta binaries and uploaded
logs, but still require Windows runtime testing. In particular, test repeated
editor open/close, swipe selection, BPM guides, Start/Stop playtest, long
horizontal panning, zooming, and the moving right-side black region.
