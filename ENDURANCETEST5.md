# EnduranceTest5

Focused regression correction over EnduranceTest4.

## Corrected regressions

- Z/X no longer trust the checkpoint callbacks to reject Normal Mode. Every
  backend proves Practice Mode first and otherwise performs no callback.
- Legacy ARM resolves the original no-argument `UILayer::onCheck()` and
  `UILayer::onDeleteCheck()` ABI, plus `PlayLayer::getUILayer()` and
  `PlayLayer::getPracticeMode()`.
- The host no longer calls `LevelEditorLayer::updateGridLayer`. EnduranceTest4's
  guessed playtest state flickered and repeatedly rebuilt editor state.
- The editor scissor/viewport sanitizer is removed. It did not solve the void.

## Retained editor work

- Song guide updates every rendered editor frame.
- BPM/time markers refresh at editor frame 1 and periodically afterward.
- Overlay counters reset when a new editor layer appears, so reopening the
  editor starts at frame 1 instead of using a process-wide counter.

## Legacy music

MCI volume is now enforced over a 45-frame settling window after every music
restart, not only once immediately after `play`, `resume`, or `seek` returns.
The log reports `Audio MCI volume guard active` when this path runs.

## Explicitly still open

The moving right-side black editor region remains unresolved. EnduranceTest5
removes the failed experiment and prevents further editor damage while the real
owner of that stale render state is investigated.
