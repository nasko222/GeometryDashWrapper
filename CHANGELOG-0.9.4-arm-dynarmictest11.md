# 0.9.4-arm-dynarmictest11

## Internet

- Fixed `getsockopt(SO_ERROR)` success translation. Test10 converted host error `0` into guest `EIO` (`5`), causing ARM libcurl to reject a successfully connected socket.
- Zero is now preserved as zero; only real Winsock errors are translated.
- Added bounded diagnostics for `SO_ERROR`, first successful send, first successful receive, and non-`EWOULDBLOCK` send/receive failures.

## External links

- Added a validated direct hook for `cocos2d::CCApplication::openURL(const char*)`.
- The hook preserves the URL argument, bypasses fragile JNI lookup behavior, and opens only HTTP/HTTPS links through the default Windows browser.
- The existing `Cocos2dxActivity.openURL` JNI handler remains as a fallback.

## Retained

- Test10 nonblocking connection completion.
- Test9 Fix1 safe APK-member hooks and persistent asset cache.
- Windows audio bridge.
- Save stability, clean shutdown, text-safe Space input, and high-performance GPU preference.
