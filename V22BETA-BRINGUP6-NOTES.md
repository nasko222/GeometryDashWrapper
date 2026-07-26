# Geometry Dash 2.2 beta ARMv7 Bringup6

## Level crash after successful decompression

Bringup5 decoded the official-level payload correctly, but manually fabricated an old libstdc++ COW string representation. Both supplied beta libraries then failed inside `PlayLayer::prepareCreateObjectsFromSetup`, proving the payload bytes were valid while the returned C++ object was not ABI-authentic.

Bringup6 discovers two values from each beta's original `ZipUtils::decompressString` implementation before installing the host hook:

1. the guest routine used to append/assign a `(char*, length)` payload;
2. the relocated `_S_empty_rep_storage + 12` data pointer used to initialize an empty string.

The host decoder initializes the hidden return object with that exact guest singleton, runs the beta's own byte routine through Dynarmic, and reads the result back for a full byte-for-byte validation. No guessed COW refcount, capacity, allocator, or singleton address remains.

Static discovery results:

- newer beta: builder `0x1062bc71`, empty data `0x1092b1bc`;
- earlier beta: builder `0x105d06f1`, empty data `0x108c6408`.

## `libgame.so` and the editor

The newer APK's `lib/armeabi-v7a/libgame.so` is an editor/GDPS companion module. It depends on `libcocos2dcpp.so`, exports editor-layer hook groups, and contains a replacement for `CreatorLayer::canPlayOnlineLevels()` whose complete behavior is:

```asm
movs r0, #1
bx   lr
```

Bringup6 reproduces that exact four-byte behavior directly in the emulated main library on both known betas. This avoids attempting to execute the companion's Android Dobby/hooking engine on Windows while still applying the editor eligibility fix it was designed to install.

The older `CreatorLayer::onOnlyFullVersion -> onMyLevels` redirect remains as a fallback but is no longer treated as the primary unlock.

## Packaging

- no APK files;
- no historical changelog pile;
- complete source and dependency preparation scripts retained.
