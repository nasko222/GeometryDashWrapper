# Source contents — 0.9.5-unified4

- `src/backends/x86/`: native x86 Android ELF loader/runtime based on
  `0.9.3-alpha3`.
- `src/backends/arm_legacy/`: Dynarmic ARMv5TE runtime based on
  `dynarmictest14-fix1`.
- `src/backends/armv7/`: Dynarmic ARMv7/Thumb-2 runtime based on
  `0.9.4-milestone1`.
- `src/shared/`: shared Windows audio, storage, APK audio extraction, network
  compatibility, runtime settings and custom-first/official-fallback song transport.
- `third_party/`: source dependencies and their licenses.
- `cmake/`, build scripts and launchers: reproducible unified Windows build.

No Unicorn code, F2 editor hotkey, APK, extracted game library, generated EXE,
DLL or build cache is included.

- `src/shared/window_icon_win.*`: live Win32 icon application from the APK or optional `icon.png`.
