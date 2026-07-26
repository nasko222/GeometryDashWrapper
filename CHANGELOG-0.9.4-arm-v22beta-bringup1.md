# 0.9.4-arm-v22beta-bringup1

- Forked the stable Test14 source into a separate Geometry Dash 2.2 beta branch.
- Added raw `libcocos2dcpp.so` and full-APK input auto-detection.
- Switched the Dynarmic guest architecture from ARMv5TE to ARMv7-A.
- Added Thumb-2, VFPv3 and NEON startup smoke validation.
- Added the 2.2 beta ZIP-hook prologue and removed the obsolete second ZIP-hook requirement.
- Added all GLES imports required by the supplied library.
- Added an initial 28-function FMOD guest-object/audio bridge.
- Added explicit compatibility handlers for the supplied library's libc/POSIX import set.
- Added a generated import manifest and static-audit mode.
- Added separate raw-library and complete-APK launchers.
- Preserved every Test14 source/history/dependency file and the original legacy `game.apk`.
