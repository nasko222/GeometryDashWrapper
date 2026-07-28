# Building Unified6

Run `BUILD_ALL.cmd` on 64-bit Windows. Output is placed in `dist-unified`.
The source archive contains no APK or extracted game library.

Put the APK at `dist-unified\game.apk`, edit `GeometryDash.cfg`, and open
`GeometryDash.exe`. The launcher selects x86 first, then legacy ARM, then ARMv7.
Python 3 remains required for APK inspection and automatic backend selection.
