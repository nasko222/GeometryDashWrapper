# DynarmicTest7 notes

This build fixes the two save-path crashes reported in DynarmicTest6 and enables the existing Windows audio bridge for the Dynarmic executable.

## Save crash root cause

DynarmicTest6 returned synthetic `FILE*` handles in the unmapped `0x23000000` address range. Most imported stdio calls worked because the host intercepted the exact pointer value, but pugixml/Bionic inspected fields inside the guest `FILE` object while closing a save stream. The first byte access at `FILE* + 0x0d` therefore stopped Dynarmic with an invalid guest-memory error.

This affected both:

- automatic save during pause/level switching (`CCGameManager.dat`)
- editor level save (`CCLocalLevels.dat`)

## Test7 changes

- Every host file now has a real 256-byte object inside mapped guest heap memory.
- The Bionic read/write flags and descriptor fields are initialized.
- Closed non-standard streams release the guest object back to the free-list allocator.
- `__sF` stdin/stdout/stderr aliases resolve to the new mapped objects.
- Unknown/double `fclose` calls are logged without touching invalid memory.
- The existing Windows MCI audio implementation is linked into the Dynarmic x64 target.
- Background music, effects, preload, pause/resume, volume, seeking and effect controls are connected to their JNI methods.
- MP3 music and OGG/WAV effects are extracted or decoded from `game.apk` into `save/audio-cache`.
- Test5 fatal diagnostics and Test6 reclaiming heap remain enabled.

## Expected first audio run

The first launch may spend extra time extracting `menuLoop.mp3` and decoding OGG effects. Later launches reuse the cache.
