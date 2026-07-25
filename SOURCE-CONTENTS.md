# DynarmicTest5 source contents

This is a full working-source package based on the corrected 33.9 MB DynarmicTest4 full-source tree, with Test5 fatal diagnostics added.

## Explicitly retained

- `game.apk`
- Entire `src/` directory, including the Dynarmic runtime and existing Unicorn wrapper sources
- Entire `third_party/` directory, including zlib, STB, Unicorn archive/license content
- Entire `tools/` directory
- Entire `patches/` directory
- `BUILD_DYNARMIC_X64.cmd`, `build-dynarmic-x64.ps1`, and older build scripts required by retained source
- `dynarmic-x64/CMakeLists.txt`
- Current Test5 documentation and version metadata

## Removed or replaced only as obsolete documentation

- Test4 milestone notes/changelog, replaced by Test5 files
- No source file, APK, vendor dependency, build helper, or license

No destructive cleanup script is included.
