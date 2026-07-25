# 0.9.4-arm-dynarmictest7

- Fixed invalid guest memory at fake `FILE* + 0x0d` while pugixml closed save files.
- Fixed the same underlying crash when switching levels, pausing, and saving editor levels.
- Replaced unmapped synthetic file handles with mapped guest `FILE` objects.
- Reclaims guest file objects after close and preserves `__sF` standard-stream behavior.
- Enabled the existing Windows MCI audio backend in the Dynarmic x64 build.
- Wired Cocos2d JNI music/effect playback, volume, pause/resume, seek, preload and effect controls.
- Added APK audio extraction/OGG decoding support to the Dynarmic target.
- Retains the Test6 allocator fix, 256 MiB heap and Test5 diagnostics.
