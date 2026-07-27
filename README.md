# Geometry Dash ARM Wrapper — NetworkTest7

`networktest7-native-winhttp-bridge` replaces the entire emulated Cocos2d-x
HTTP worker path with a native Windows transport bridge.

The supplied 2.2 beta normally sends requests through an emulated ARM pthread,
libcurl, OpenSSL, DNS, and socket stack. NetworkTest2 through NetworkTest6 tried
to preserve that stack while changing its scheduling. The NetworkTest6 trace
shows the worker progressing past the earlier synthetic-import resume problem,
then stopping in OpenSSL `CRYPTO_THREAD_run_once` before DNS or socket creation.

NetworkTest7 therefore hooks only:

- `cocos2d::extension::CCHttpClient::send(CCHttpRequest*)`

At that boundary it:

1. Copies the request method, URL, body, and headers from guest memory.
2. Retains the original guest `CCHttpRequest`.
3. Returns immediately so the button animation and render loop continue.
4. Performs HTTP/HTTPS through WinHTTP on a real Windows host thread.
5. Builds an ABI-compatible guest `CCHttpResponse` on the main frame thread.
6. Invokes the request's original Cocos2d member callback.
7. Releases the response through the guest `CCObject` destructor path, which
   releases the retained request exactly once.

The guest CCHttpClient pthread, libcurl, OpenSSL, DNS, and socket code is not
entered for these requests. All editor, save, audio, platformer, APK-cache,
inflate, keyboard, lifecycle, and companion-library behavior remains based on
NetworkTest6.

## Build

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Expected output directory:

```text
dist-arm-wrapper-v22beta-networktest7-native-winhttp-bridge\
```

Primary diagnostics:

```text
gd-networktest7.log
gd-networktest7-imports.txt
gd-networktest7-profile.csv
gd-networktest7-profile-summary.txt
```
