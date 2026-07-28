# Building Unified5

Run `BUILD_ALL.cmd` on Windows. Output is placed in `dist-unified`.
The source archive contains no APK or extracted game libraries.

Put the APK at `dist-unified\game.apk`, edit `RUN_AUTO.cmd`, then run it.
The launcher selects x86 first, then legacy ARM, then ARMv7.
