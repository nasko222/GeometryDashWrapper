# Geometry Dash 2.2 beta ARMv7 Bringup5

## Level data root cause

Bringup4 recovered a null `CCString` by substituting an empty settings header. That prevented the first crash but could only create an empty level. The next launch then failed during RTTI because the expected settings/object graph had never been constructed.

The actual source string is loaded into `GJGameLevel`, then passed to `cocos2d::ZipUtils::decompressString(std::string, bool, int)` before `PlayLayer::prepareCreateObjectsFromSetup`. The supplied APK's level values are valid URL-safe Base64-wrapped GZIP payloads. As a concrete check, level 4001 expands from 165,068 encoded bytes to 1,241,094 bytes beginning with a valid Geometry Dash setup header.

Bringup5 hooks that exact C++ method while preserving the hidden structure-return pointer and all ARM arguments. The host bridge:

1. reads the ARM libstdc++ COW input string;
2. applies the beta's optional byte-XOR mode;
3. decodes standard or URL-safe Base64;
4. inflates GZIP, zlib, or raw-deflate payloads;
5. allocates a valid guest COW string representation and returns the full setup.

The previous null/empty-level recovery was removed.

## Creator editor gate

The beta includes both the real `CreatorLayer::onMyLevels` callback and a deliberate `CreatorLayer::onOnlyFullVersion` gate. Bringup5 intercepts the gate, logs it, and tail-calls the genuine My Levels callback with the original `this` and sender arguments intact.

## Retained fixes

- FMOD 1.05.04-compatible deferred music playback;
- 60 Hz Android refresh-rate response;
- ARMv7 exclusive-monitor callbacks;
- both known `CCApplication::openURL` prologues;
- no APK files in the source archive.
