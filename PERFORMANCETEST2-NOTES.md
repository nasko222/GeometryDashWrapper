# PerformanceTest2

This is based on PerformanceTest1, not the Overkill branch.

`RUN_ARM_NATIVE_BOOT.cmd` uses both the fully extracted APK directory and aggressive Unicorn translation warm-up. `RUN_NO_PRETRANSLATE_BASELINE.cmd` uses the extracted APK but lets Unicorn translate normally on demand.

A blind permanent ARM-to-x86 conversion of every byte is unsafe because executable ELF segments also contain literal pools, jump tables, mixed ARM/Thumb entry points, and indirect control flow. The warm-up therefore asks Unicorn/TCG to cache blocks at dense intervals across executable segments and keeps every accepted block in the 256 MiB TCG cache.
Build hotfix 1 adds the missing local ELF `PF_X` definition required by the Windows compiler. It does not change runtime behavior.

