# EnduranceTest10

Focused 2.2 beta editor correction based on the EnduranceTest9 freeze log.

- clamps the community-expanded art selectors to the assets actually present in
  the selected APK: 18 grounds and 26 backgrounds;
- prevents null-texture SpriteBatchNode construction instead of attempting to
  recover after the invalid object already exists;
- redirects all three exact `updateCameraBGArt` callsites and suppresses them
  only for the active LevelEditorLayer, including editor playtest camera paths;
- calls the original background routine for normal PlayLayer gameplay;
- preserves the working BPM guidelines, song guide, selection rectangle,
  object culling, Practice Z/X, networking, backups and isolated saves;
- leaves the complete x86 backend and frame pacing unchanged;
- makes no legacy music change.

The new behavior requires testing with the exact Windows/APK setup before it can
be called runtime-confirmed.
