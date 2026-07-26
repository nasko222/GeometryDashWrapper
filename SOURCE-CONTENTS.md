# Source contents

The package preserves the full DynarmicTest7 source tree and `game.apk`.

Important Test8 files:

- `src/dynarmic_probe.cpp` — ARM execution, APK memory handles, JNI/input/shutdown dispatch, guest stdio and audio dispatch
- `src/audio_win.c` / `src/audio_win.h` — Windows MCI music/effect backend
- `src/apk_extract_audio.c` — minimal APK ZIP member extraction for audio
- `third_party/stb/stb_vorbis.c` — OGG decoding
- `third_party/zlib/` — APK extraction and cache compression support
- `dynarmic-x64/CMakeLists.txt` — x64 Dynarmic target and Windows audio libraries
- `build-dynarmic-x64.ps1` — pinned Windows build workflow
- `game.apk` — preserved input APK

No cleanup step removes source dependencies or the APK.
