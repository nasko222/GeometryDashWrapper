# 0.9.4-arm-dynarmictest10

## Internet

- Fixed nonblocking Winsock `connect()` completion. Test9 returned Linux `EAGAIN` for Windows `WSAEWOULDBLOCK`, so the ARM copy of libcurl immediately reported `couldn't connect to host` even though the TCP connection was still being established.
- Test10 waits for the pending Windows connection with `select()`, verifies `SO_ERROR`, and returns a connected socket or a real mapped error.
- Added explicit connection diagnostics: immediate success, completed pending connection, timeout, or concrete Winsock failure.

## External links

- Implemented `Cocos2dxActivity.openURL(Ljava/lang/String;)V`.
- HTTP and HTTPS links are opened through the Windows default browser using `ShellExecuteW`.
- Other URI schemes and local paths are rejected.

## Retained

- Test9 Fix1 safe APK-member hook.
- Persistent host APK-member cache.
- Windows audio bridge.
- Save stability, clean shutdown, text-safe Space input, and high-performance GPU preference.
