# Source contents

## Provenance

- x86 and legacy ARM base archive: `GeometryDashArmWrapper-0.9.4-arm-dynarmictest14-fix1-full-source.zip`
  (`ba8ca4bafdd3c4e5cf1c95d2c275c0c994b4e5654dfa20f264f823bc646e9c42`).
- ARMv7 base archive: `GeometryDashWrapper-0.9.4-milestone1-full-source-no-apk.zip`
  (`acd75f8aa876365788c4338de79644986e9a249ce12c9e308264192d81025b90`).

## Retained

- all source required for the three wrapper backends;
- shared storage, audio, APK-audio, network compatibility, and launch-settings
  modules;
- zlib and stb_vorbis source plus licenses;
- the Dynarmic/Zig toolchain wrappers used by the build;
- concise current documentation and portable build scripts.

## Excluded

- every APK and extracted proprietary `.so`;
- all Unicorn source, patches, licenses, toolchains, and backend code;
- old bootstrap/performance/network-test notes, logs, audits, checksum reports,
  source-update bundles, generated executables, and build caches.

## Unified2 changes

- adds four editable root-BAT settings: GDPS server, icon unlock checks,
  spin-off full-version bypass, and ARM-over-x86 override;
- preserves x86 as the normal first-priority architecture;
- shares settings parsing and API URL/request rewriting across the backends;
- keeps one root `dist-unified/save/` folder;
- keeps F2 removed and keeps normal/debug launch paths separate.
