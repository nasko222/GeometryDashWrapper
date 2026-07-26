# DynarmicTest11 technical notes

## Why Test10 still reported "couldn't connect to host"

Test10 correctly completed the nonblocking Winsock connection. The log proved that the TCP socket became writable and `SO_ERROR` on Windows was zero.

The remaining bug was in the generic Winsock-to-Android errno translator. Its fallback intentionally converted an unspecified error into guest `EIO` (`5`), but it also converted the valid success value `0` into `5`. When the ARM libcurl code called `getsockopt(SOL_SOCKET, SO_ERROR)`, it therefore received an error on a socket that was already connected and returned `CURLE_COULDNT_CONNECT`.

Test11 returns zero unchanged. Nonzero Winsock errors continue through the existing POSIX mapping.

## Direct browser bridge

The game calls:

`cocos2d::CCApplication::openURL(const char*)`

That function normally performs a JNI lookup for:

`org/cocos2dx/lib/Cocos2dxActivity.openURL(Ljava/lang/String;)V`

Test11 keeps the JNI handler but also patches the exported Thumb function directly to a host import. The hook preserves the URL argument in R1, validates the exact `0xB530` prologue, accepts only HTTP/HTTPS URLs, and launches the Windows default browser with `ShellExecuteW`.

## Expected log markers

Successful GDPS request:

```text
[host] Socket connect fd=<id> target=<ip>:80 status=connected pending=yes wait_ms=<time>
[host] Socket SO_ERROR fd=<id> host=0 guest=0
[host] Socket first send fd=<id> bytes=<count>
[host] Socket first recv fd=<id> bytes=<count>
android log: response code: 200
```

Successful external link:

```text
RESULT: DYNARMIC_CCAPPLICATION_OPENURL_HOOK_READY count=1
[host] Browser open url=https://... result=ok shell_code=<value>
```
