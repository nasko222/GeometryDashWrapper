# Geometry Dash 2.2 beta ARMv7 bringup3

The newer-beta log proved that adding only `Dynarmic::ExclusiveMonitor` was incomplete. Constructor 1 spins at an ARM `LDREX`/`STREX` atomic increment because Dynarmic delegates the final compare-and-write to `UserCallbacks::MemoryWriteExclusive*`; the previous environment inherited callbacks which always failed. Bringup3 implements all A32 exclusive write widths and verifies them before loading the game.

The earlier beta uses a different but valid `CCApplication::openURL` prologue (`0xB51F` instead of `0xB530`). Bringup3 validates and accepts both exact known forms.

No APK files are shipped. Pass an APK path to the build command or copy one into the generated distribution folder.
