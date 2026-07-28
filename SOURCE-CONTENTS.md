# Source contents — 0.9.5-unified2-fix2

## Provenance

- x86 and legacy ARM base: `0.9.3-alpha3` plus
  `0.9.4-arm-dynarmictest14-fix1`.
- ARMv7 base: `0.9.4-milestone1`.
- Unified1 supplies the known-good unified launcher, architecture priority and
  single shared save-folder behavior.

## Fix2 changes

- replaces direct `CreatorLayer::onOnlyFullVersion -> onMyLevels` patches with
  `MenuLayer::onFullVersion -> MenuLayer::onCreator` redirects;
- removes the configurable `canPlayOnlineLevels` force-true patch;
- limits `HACK_ICONS` to icon ownership rather than color ownership;
- routes `getGJSongInfo.php` to official HTTPS Boomlings for all three
  backends, while leaving song CDN URLs untouched;
- keeps configurable GDPS routing for the remaining game API endpoints;
- retains x86-first selection, ARM override, one root `save/`, no F2 and no
  Unicorn.

## Included

- complete source for x86, legacy ARM Dynarmic and ARMv7 Dynarmic backends;
- shared storage, audio, APK-audio, network compatibility, runtime settings and
  official-song transport modules;
- required zlib/stb source and licenses;
- portable build and automatic-launch scripts.

## Excluded

- APKs and extracted proprietary `.so` files;
- executables, DLLs, build caches and downloaded toolchains;
- all Unicorn source, libraries and backend code;
- obsolete bootstrap/performance/network-test notes and logs.
