# V22 Beta NetworkTest2 — async DNS

This branch fixes the client freeze seen in NetworkTest after `worker slice #2`.

## Root cause

The frame scheduler limited guest ARM execution, but a guest import can call a host API that blocks before Dynarmic gets control back. The first network resolver call used synchronous WinSock `getaddrinfo()` on the same UI thread. Because the old DNS log was written only after `getaddrinfo()` returned, a blocked resolver left the log ending at the start of worker slice #2.

## Changes

- `getaddrinfo` and `gethostbyname` are resolved on detached native DNS threads.
- The guest worker remains parked on the import stub until the result is ready.
- DNS completion wakes the worker on a later frame.
- An 8-second timeout resumes the guest with a resolver error instead of freezing.
- Worker slices are reduced to 2 ms / 12 runs and log PC/LR at slice entry.
- Worker-side `getnameinfo` is forced to numeric output so reverse DNS cannot block.
- Sockets remain nonblocking.

Build with `BUILD_V22BETA_X64.cmd`, then run `RUN_NETWORKTEST2.cmd`.
The focused log is `gd-networktest2.log`.
