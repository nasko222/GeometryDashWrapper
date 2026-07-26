# 0.9.4-arm-dynarmictest9

## Loading acceleration

- Hooks cocos2d-x `CCFileUtils::getFileDataFromZip` and `existFileDataFromZip` before guest execution begins.
- Builds the APK central-directory index once on the host.
- Decompresses each requested APK member natively instead of executing the guest minizip scanner.
- Reuses decompressed members from an in-process memory cache.
- Persists validated decompressed members below `save/apk-member-cache/<apk fingerprint>` for later launches.
- Keeps editable game saves and editor level data outside this cache.

## Lower-end GPU behavior

- Exports `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` so hybrid-GPU systems prefer the discrete GPU.
- Logs the selected OpenGL vendor, renderer and version.
- Samples repetitive touch-move diagnostics instead of writing every movement to disk.

## Experimental internet bridge

- Initializes Winsock 2.2 and maps the Android socket API used by the bundled libcurl to Windows sockets.
- Implements DNS, IPv4/IPv6 address conversion, connect, send/receive, poll, nonblocking mode and common socket options.
- Runs the game’s `CCHttpClient` worker as a cooperative guest coroutine when its semaphore is posted.
- Logs DNS and initial socket connection attempts for diagnostics.

## Retained fixes

- Test6 reclaiming guest allocator.
- Test7 mapped `FILE` objects, stable saves and Windows audio.
- Test8 clean Exit handling and text-safe Space input.
