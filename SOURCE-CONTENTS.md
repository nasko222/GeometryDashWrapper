# DynarmicTest4 source contents

This is a full working-source package based on the intact DynarmicTest3 Fix1 tree with the Test4 interactive runtime overlaid.

## Explicitly retained

- `game.apk`
- Entire `src/` directory, including the Dynarmic runtime and the existing Unicorn wrapper sources
- Entire `third_party/` directory, including zlib, STB, Unicorn archive/license content
- Entire `tools/` directory
- Entire `patches/` directory
- `BUILD_DYNARMIC_X64.cmd`, `build-dynarmic-x64.ps1`, and older build scripts required by the retained source
- `dynarmic-x64/CMakeLists.txt`
- Current Test4 documentation and version metadata

## Removed only as obsolete documentation/history

- Bootstrap and PerformanceTest changelogs/notes
- DynarmicTest1–3 milestone notes/changelogs
- Old migration notes
- Obsolete bootstrap source patch artifact

No source file, APK, vendor dependency, build helper, or license was removed during documentation cleanup. No destructive cleanup script is included.
