# DynarmicTest9 technical notes

## Why Test8 did not substantially shorten loading

Test8 stored `game.apk` in host memory, but the original cocos2d/minizip code still searched the ZIP from inside the ARM guest. A single level transition could still cross the Dynarmic-to-host import boundary more than five million times.

Test9 patches the two exported Thumb functions used to access APK members:

- `cocos2d::CCFileUtils::getFileDataFromZip`
- `cocos2d::CCFileUtils::existFileDataFromZip`

The replacement host imports preserve the original allocation and size-return behavior while avoiding the guest ZIP loop.

## Cache safety

Only immutable members from `game.apk` are cached. `CCGameManager.dat`, `CCGameStore.dat`, `CCLocalLevels.dat`, editor data and other writable files continue through the normal mapped guest `FILE` bridge. Cache records are keyed by APK fingerprint and member CRC/size and are CRC-validated before reuse.

Expected log markers:

```text
RESULT: DYNARMIC_CCFILEUTILS_ZIP_HOOKS_READY count=2
RESULT: DYNARMIC_APK_MEMBER_INDEX_READY entries=...
[host] APK member cache ...
Dynarmic APK member cache totals: ...
```

## Internet implementation

The original game uses a background `CCHttpClient` worker and an embedded libcurl. Earlier wrapper builds reported network availability but returned failure from every socket import and never executed `pthread_create` workers.

Test9 maps the socket imports to Winsock and stores the CCHttp worker CPU context when it waits on its semaphore. `sem_post` resumes that context synchronously until the request has been processed and the worker waits again. The normal game scheduler then dispatches the queued response callback.

Expected markers include:

```text
RESULT: DYNARMIC_WINSOCK_BRIDGE_READY version=2.2
[host] Cooperative guest worker registered ...
[host] DNS getaddrinfo ...
[host] Socket connect ...
```

This internet path is new and should be treated as experimental until tested against the current game servers.

## Friend performance log

The Test7 log contains Intel-specific OpenGL extensions despite the machine reportedly having a GTX 1050. Test9 asks Windows hybrid-graphics drivers to select the high-performance GPU and prints the actual renderer so this can be verified directly.
