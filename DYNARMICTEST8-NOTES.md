# DynarmicTest8 technical notes

## Why level and first-menu loads were slow

A Test7 level-open diagnostic showed roughly four million guest `fread` calls in five seconds while the game repeatedly scanned `game.apk`. The APK was already loaded by the wrapper, but each guest open still created a host `FILE*`, each tiny read allocated a temporary vector, and every import trap generated diagnostic-history work.

Test8 keeps a pointer to the immutable APK byte vector for the lifetime of `GuestExecutor`. Read-only APK opens create normal mapped guest `FILE` objects whose backing store is that vector. Seek/tell/read semantics remain per-handle, so multiple guest ZIP readers may operate independently.

This is resource/APK caching, not parsed level-object caching. Parsed level/editor objects can change after editing and should remain owned by the original game.

## New log evidence

At shutdown Test8 prints:

```text
Dynarmic APK memory cache totals: reads=<count> bytes=<count>
```

During the first few opens it prints:

```text
Dynarmic APK memory open: ... bytes=...
```

A successful Exit-button path prints:

```text
Dynarmic clean shutdown requested by guest terminateProcess
RESULT: DYNARMIC_BRINGUP8_OK
```

## Text input

The host tracks Android `openIMEKeyboard`, `closeIMEKeyboard`, `showEditTextDialog`, and `setKeyboardState`. While active, Win32 Space keydown is not converted to the gameplay touch bridge; `WM_CHAR` still sends the space to `nativeInsertText`.

## Scope

Friend-specific low-end performance tuning is intentionally not guessed from a Test6 report. The friend should test Test8 and provide its full log, which includes the current allocator, audio and APK-memory behavior.
