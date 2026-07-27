# Building 0.9.5-unified1-fix1

Use 64-bit Windows 10/11 with Python 3 and Git for Windows. The command files
download portable Zig, CMake, Ninja, Boost, and the pinned Dynarmic revision into
`.build-tools/`; nothing is installed system-wide.

## Build everything

Run `BUILD_ALL.cmd`. It builds in this order:

1. x86 native (`0.9.3-alpha3`)
2. legacy ARM and ARMv7 against one Dynarmic build

Output:

```text
dist-unified/
  game.apk
  save/
  x86/
  arm-legacy/
  armv7/
  RUN_AUTO.cmd
  run_auto.py
```

Place the APK at `dist-unified/game.apk` and run `RUN_AUTO.cmd`. Native x86 is
always selected first when the APK also includes ARM libraries.

## Individual builds

- `BUILD_X86.cmd [optional-x86-apk]`
- `BUILD_DYNARMIC.cmd`

The generated backend-specific `RUN.cmd` files also execute from the
`dist-unified` root and therefore use the same `save/` folder and root
`game.apk`.
