# NetworkTest4: DynarmicTest14 wake semantics + forensic network trace

NetworkTest4 is based directly on NetworkTest3/NetworkTest2 so all non-network
fixes remain in place. The network worker behavior is changed only where the
logs and DynarmicTest14 comparison identify a meaningful difference.

## Actual behavioral fix

DynarmicTest14 resumes its registered guest HTTP worker immediately from
`sem_post`, before returning to the foreground guest. NetworkTest2 and
NetworkTest3 changed that to a deferred next-frame wake. NetworkTest4 restores
the immediate wake behavior for both semaphore posts and condition signals,
while retaining the short frame slices, asynchronous DNS, nonblocking sockets,
and the hard 4 ms Dynarmic `HaltExecution` watchdog.

## Forensic tracing

The runtime now records every imported call in a fixed 512-entry ring without
formatting a string on every hot libc trap. It additionally:

- logs every thread, semaphore, condition-variable, socket, DNS, send/receive,
  poll, and Android-log import with caller address and r0-r3;
- emits a guest heartbeat every 250 ms during long root guest calls;
- dumps the last 128 imports after any native call taking at least 250 ms;
- dumps the ring on a worker watchdog preemption or execution failure;
- marks request-like Android log payloads before `pthread_create`/socket work;
- writes the import manifest in actual runtime SVC/stub order and also retains
  the alphabetical index.

These diagnostics distinguish four separate failure points: request creation,
worker creation/wake, DNS, and socket I/O.

## Log files

- `gd-networktest4.log`
- `gd-networktest4-imports.txt`
- `gd-networktest4-profile.csv`
- `gd-networktest4-profile-summary.txt`
