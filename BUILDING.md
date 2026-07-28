# Building 0.9.5-unified2

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

Open `dist-unified/RUN_AUTO.cmd` to configure `GDPS_SERVER`, `HACK_ICONS`,
`FULL_BYPASS`, and `OVERRIDE_ARM`. Native x86 remains first priority unless
`OVERRIDE_ARM=true` and the APK contains a supported ARM library.

## Individual builds

- `BUILD_X86.cmd [optional-x86-apk]`
- `BUILD_DYNARMIC.cmd`

The generated backend-specific `RUN.cmd` files execute from the `dist-unified`
root and therefore use the same `save/` folder and root `game.apk`. Direct
backend launchers use safe defaults from the runtime: official Boomlings API,
icon hack disabled, and full-version bypass enabled.
