# Geometry Dash ARM Wrapper — NetworkTest8

NetworkTest8 keeps the native `CCHttpClient::send` bridge introduced in NetworkTest7, but removes its single serial WinHTTP queue. Every request now runs on its own host thread, uses direct WinHTTP with bounded timeouts, and reports each transport stage back to the main log.

Key changes:

- independent native thread per HTTP request;
- no guest pthread/libcurl/OpenSSL execution;
- no serial head-of-line blocking;
- direct connection (`WINHTTP_ACCESS_TYPE_NO_PROXY`);
- resolve/connect/send/receive timeouts: 5/5/10/15 seconds;
- automatic `Content-Type: application/x-www-form-urlencoded` for POST/PUT;
- detailed stages: thread-start, crack-url, open-session, connect, open-request, send, receive, headers, finish/error;
- original guest response callback remains on the main frame thread.

Build with:

```bat
BUILD_V22BETA_X64.cmd game-v22beta-selected.apk
```

Output directory:

```text
dist-arm-wrapper-v22beta-networktest8-concurrent-winhttp
```

Logs:

```text
gd-networktest8.log
gd-networktest8-imports.txt
gd-networktest8-profile.csv
gd-networktest8-profile-summary.txt
```
