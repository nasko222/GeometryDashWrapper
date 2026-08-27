# Building Geometry Dash Wrapper 0.9.7-cof2 on Windows

Use a fresh extracted COF1 source directory on 64-bit Windows and run:

```bat
BUILD_ALL.cmd
```

This builds the x86 backend, legacy ARM Dynarmic backend, ARMv7 Dynarmic
backend, and native launcher into:

```text
dist-unified\
```

The build scripts fetch their pinned public toolchain/dependencies as needed.
The ARMv7 builder revision for this branch is
`dynarmic-x64-builder106-0.9.7-cof2`.

Do not reuse a `gdpstweaks16` ARMv7 executable: the point of COF1 is to compile
out the stock editor reconstruction path and return to the Endurance/companion
handler.

After building, drag the desired APK onto `RUN_AUTO_GDPS.cmd` or
`RUN_AUTO_BOOMLINGS.cmd`. The modded selected 2023 beta should contain its own
compatible `lib/armeabi-v7a/libgame.so`; COF1 does not bundle or copy that
proprietary library.
