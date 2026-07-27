# V22 beta Bringup19 — selected APK desktop/network pass

## Scope

This branch targets the selected 144,490,721-byte APK and its own embedded
`libgame.so`. Stock SubZero and the 95 MB beta use different editor/runtime
layouts and are no longer treated as interchangeable targets.

## Local saves only

The game still passes Android-looking paths to libc, but the wrapper intercepts
those strings and maps the relative filename directly into `save-v22beta`.
Bringup19 removes the mounted-drive legacy migration scan and the recovery
scripts. It never searches `D:\data\data` or another drive-root Android tree.

## Desktop text entry

The mobile build asks whether a software keyboard covers the selected field and
moves the entire scene upward. On Win32 there is no software keyboard. The
wrapper patches the primary APK's keyboard-show/hide callbacks and every
`textInputShouldOffset` delegate to remain stationary while preserving normal
`WM_CHAR` text input.

## Networking

The selected APK imports several POSIX calls that Bringup18 still returned as
empty stubs. Bringup19 implements:

- `gethostbyname` with a real 32-bit bionic `hostent`
- `getnameinfo`
- `shutdown`
- `writev`
- loopback-backed `pipe` and `socketpair`

The existing `getaddrinfo`, socket, connect, send/receive, poll, fcntl, ioctl and
cooperative CCHttpClient worker remain enabled. Logs now show DNS/socket traffic
when GD servers or a custom-song request is attempted.
