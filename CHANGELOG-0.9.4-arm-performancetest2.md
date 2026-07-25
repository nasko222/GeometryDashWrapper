# 0.9.4-arm-performancetest2

- Returns to the stable PerformanceTest1 branch.
- Pre-extracts the complete APK into `apk-unpacked` during the Windows build.
- Serves indexed Cocos ZIP asset requests from ordinary pre-extracted files first.
- Adds `--pretranslate-all`, which aggressively requests Unicorn translation blocks across executable ELF segments before constructors and retains the TCG cache.
- Logs requested, cached, rejected blocks and warm-up duration.
- Includes a baseline launcher without aggressive pretranslation for direct comparison.
## Windows build hotfix 1

- Defines the ELF program-header permission flags locally (`PF_X`, `PF_W`, and `PF_R`) so the aggressive pretranslation scanner compiles with Zig's Windows C environment.

