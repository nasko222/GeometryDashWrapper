# Source contents — DynarmicTest9

The package contains the complete Test8 source tree plus the Test9 changes. `game.apk` is preserved byte-for-byte.

Important files:

- `src/dynarmic_probe.cpp` — ELF loading, Dynarmic execution, guest allocator, JNI, files, APK member cache, GPU preference, cooperative worker and Winsock bridge.
- `src/audio_win.c` / `src/audio_win.h` — Windows music/effect backend.
- `src/apk_extract_audio.c` — host extraction used by the audio bridge.
- `third_party/stb/stb_vorbis.c` — OGG decoding.
- `third_party/zlib/` — compression and APK extraction support.
- `dynarmic-x64/CMakeLists.txt` — x64 Dynarmic executable and Windows libraries, including `ws2_32`.
- `build-dynarmic-x64.ps1` — pinned Windows build workflow.
- `BUILD_DYNARMIC_X64.cmd` — normal build entry point.
- `game.apk` — preserved ARM game package.
- `CHANGELOG-0.9.4-arm-dynarmictest9.md` — user-facing changes.
- `DYNARMICTEST9-NOTES.md` — implementation and diagnostic notes.

All 1,120 files from the Test8 package remain present. Test9 adds two documentation files.
