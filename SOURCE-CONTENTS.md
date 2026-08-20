# Source contents — 0.9.6-gdpstweaks9

Included:

- x86 Android wrapper source.
- Dynarmic legacy ARM and ARMv7 wrapper source.
- Shared Windows audio/network/storage/runtime bridges.
- Wrapper-owned stock 2019/2022/2023 ARMv7 2.2 beta editor restoration.
- Exact stock editor setup-field profiles: 2019 `+0x110`, 2022/2023 `+0x11C`.
- Strict host-side editor level decoding with no cross-field string heuristic.
- 2019 Lite editor sprite-frame atlas preload before `EditorUI::create`.
- ARMv7 FULL_BYPASS late CreatorLayer lock/tint bypass for register variants.
- Startup-level ARMv7 pause-item creation suppression plus the existing frame-time fallback.
- Resizable/fullscreen DPI-aware Windows host code.
- Windows build/launcher scripts and vendored third-party source/licenses.

Excluded:

- iOS backend and iOS public-test code.
- APKs, extracted proprietary game libraries, EXEs and DLLs.
