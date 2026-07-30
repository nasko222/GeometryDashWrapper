# Geometry Dash Wrapper 0.9.5 — EnduranceTest7

`endurancetest` remains the cross-version stability branch. EnduranceTest7 is a
narrow follow-up based on the July 30 logs: Z/X is preserved, all failed legacy
MCI volume-reassert experiments are removed, newly opened legacy level tracks
no longer inherit the quieter saved volume from the previous alias, and the remaining 2.2
editor work is limited to BPM initialization and the exact companion opacity
rules.

## Running

Python is not required.

- `RUN_AUTO_GDPS.cmd` uses `naskogdps17.7m.pl/database`.
- `RUN_AUTO_BOOMLINGS.cmd` uses `www.boomlings.com/database`.

Double-click a launcher to use `game.apk`, or drag an APK onto either launcher.
Version-isolated saves remain enabled by default.

## 2.2 beta editor rendering

The July 30 run confirmed that the editor layer and song guide initialize, but
no successful time-marker rebuild occurs. The beta's own code calls
`DrawGridLayer::updateTimeMarkers()` during editor setup/playback. EnduranceTest7
calls it exactly once for each newly-created editor layer. It is never called
periodically and `LevelEditorLayer::updateGridLayer()` remains untouched.

The fast visibility bridge now reproduces the opacity decisions from the exact
636-byte companion `LevelEditorLayerExt::updateVisibilityH` helper:

- special helper objects are opacity 0 when the beta's `0121` option or object
  flags require hiding them;
- ordinary non-selected editor objects use opacity 70;
- selected colour-group objects use opacity 255.

EnduranceTest6 forced every activated object to 255. The logs showed two special
objects being reactivated at full opacity every frame, making hidden oversized
helper sprites the strongest remaining explanation for the moving black region.

The existing camera culling, object activation/deactivation, colour queue,
client-array/VBO repair, song-position update, area-visual processing and batch
sorting are otherwise unchanged.

## Legacy music experiment

All volume-guard/reassert code is removed:

- no frame-by-frame maintenance function;
- no volume command after play, resume, rewind or seek;
- no automatic saved-volume command merely because a stream was opened.

The old clients preload a menu MCI alias and then submit their saved volume.
When a level track is opened later, copying that stored value into the new alias
makes attempt 1 quieter while later MCI restarts return to device volume.
EnduranceTest7 deliberately leaves each newly opened alias at MCI device volume,
so the first level attempt should match attempt 2 onward.

Normal live volume-slider changes still call `setaudio` for the stream that is
currently open. The value is intentionally not copied into a later newly opened
track in this experiment.

## Deliberately unchanged

- Z/X Practice Mode controls, now confirmed working on all tested versions;
- x86 pacing and optimisation code;
- networking, backup transport and save isolation;
- native mouse visibility and native pause-button behaviour;
- 2.2 viewport/scissor state and editor grid rebuilding.

## Runtime confirmation needed

Test the 2.2 editor with BPM enabled, repeated close/reopen, Play/Stop, long
horizontal panning and zooming. The black-region and legacy-volume changes are
evidence-based targets, not pre-test claims.
