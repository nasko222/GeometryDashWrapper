# V22 beta Bringup20 — DynarmicTest14 network wake-up port

## Root cause found in Bringup19

The current APK builds the custom-song request and creates its CCHttpClient
worker, but never reaches DNS or a socket call. Its import table contains
`pthread_cond_wait` and `pthread_cond_signal`, while it contains no `sem_wait`
or `sem_post`. Bringup19 only used semaphore wake-ups and returned success from
condition-variable calls without resuming the guest worker.

DynarmicTest14's successful design is retained for the actual worker execution,
socket bridge, DNS, poll, send and receive paths. Bringup20 extends its wake-up
model to the condition-variable synchronization used by this newer APK:

1. `pthread_create` records the guest worker context.
2. `pthread_cond_signal` supplies a pending wake token and resumes the worker.
3. `pthread_cond_wait` yields the worker without blocking the Windows UI thread.
4. The next signal resumes at the wait call, consumes the token, and lets the
   worker process the queued HTTP request.

## Scope

- Current v22 beta APK only for this test cycle.
- The known bad 90/95 MB compile is ignored, not rejected by a special hash.
- No APK size or SHA-256 allowlist.
- No donor native library.
- No mounted-drive Android save migration.
