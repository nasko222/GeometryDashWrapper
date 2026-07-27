# NetworkTest7 — native WinHTTP bridge

## Different approach

NetworkTest7 does not schedule, slice, resume, or execute the guest HTTP worker.
It hooks `CCHttpClient::send` and moves the transport layer to WinHTTP.

## Request ABI used

Validated against the selected beta's ARM ELF:

- `CCHttpRequest +0x30`: request type
- `+0x34`: ARM libstdc++ COW URL string
- `+0x38`: request-body `vector<char>`
- `+0x48`: callback target
- `+0x4c/+0x50`: ARM member-function selector pair
- `+0x58`: request-header `vector<string>`

Supported request types are GET, POST, PUT, and DELETE.

## Response ABI used

- object size: `0x58`
- `+0x30`: request pointer
- `+0x34`: transport success
- `+0x38`: response body `vector<char>`
- `+0x44`: raw response headers `vector<char>`
- `+0x50`: HTTP status code
- `+0x54`: error-buffer COW string

The bridge calls the guest `CCObject` constructor, installs the real
`CCHttpResponse` vtable, and releases the response through guest
`CCObject::release` after the callback.

## Transport

- WinHTTP host thread
- system proxy configuration
- HTTP and HTTPS
- 10 s resolve, 20 s connect, 60 s send/read timeouts
- gzip/deflate decompression when supported by the Windows SDK/runtime
- 128 MiB response safety limit

## Files intentionally changed from NetworkTest6

Runtime behavior:

- `src/dynarmic_probe.cpp`
- `dynarmic-x64/CMakeLists.txt` (`winhttp` link library)

Branch/build metadata and NetworkTest7 audit documents are also updated.
