# DynarmicTest10 technical notes

## Why Test9 internet failed

The ARM libcurl code creates a nonblocking socket. On Windows, the first `connect()` normally returns `SOCKET_ERROR` with `WSAEWOULDBLOCK` while the connection is in progress. Test9 passed this through the generic error mapper as POSIX `EAGAIN` (11). For `connect()`, libcurl expects either success or `EINPROGRESS` and treated `EAGAIN` as a hard connection failure.

Test10 completes pending connects on the host:

1. Call Winsock `connect()`.
2. On `WSAEWOULDBLOCK`, `WSAEINPROGRESS`, or `WSAEALREADY`, wait up to 15 seconds with Winsock `select()`.
3. Query `SO_ERROR`.
4. Return success only when the socket is writable and `SO_ERROR == 0`; otherwise map the actual Winsock error to the Android/POSIX errno value.

`send()` and `recv()` retain normal nonblocking `EAGAIN` behavior, and the existing guest `poll()` bridge remains available to libcurl.

## Browser bridge

The game implementation of `cocos2d::CCApplication::openURL` resolves the static Java method:

`org/cocos2dx/lib/Cocos2dxActivity.openURL(Ljava/lang/String;)V`

Test10 handles that JNI call and invokes the user's default Windows browser. Only `http://` and `https://` are accepted.

## Expected log markers

Successful GDPS connection:

```text
[host] DNS getaddrinfo node=<host> service=80 result=0
[host] Socket connect fd=<id> target=<ip>:80 status=connected pending=yes wait_ms=<time>
android log: response code: 200
```

Successful Facebook/Twitter button:

```text
JNI method: org/cocos2dx/lib/Cocos2dxActivity.openURL (Ljava/lang/String;)V
[host] Browser open url=https://... result=ok shell_code=<value>
```
