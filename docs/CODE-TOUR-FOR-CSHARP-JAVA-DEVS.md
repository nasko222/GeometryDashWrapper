# Code tour for C# and Java developers

This project is C/C++, but the architecture maps cleanly to concepts you already
know. Read this before opening the very large emulator files.

## Project map

| Folder | C#/Java analogy | Responsibility |
|---|---|---|
| `src/launcher` | Console application / bootstrapper | Inspects the APK, selects a backend, creates logs and starts the process. |
| `src/backends/x86` | Native compatibility host | Loads an Android x86 ELF directly and supplies JNI, OpenGL, audio, files and sockets. |
| `src/backends/arm_legacy` | Emulator host for an older ISA | Runs ARMv5/Thumb game code through Dynarmic. |
| `src/backends/armv7` | Emulator host for ARMv7 | Runs the 2.2-era ARMv7 client and its compatibility services. |
| `src/shared` | Shared library / service layer | Storage, settings, networking, audio and icon helpers. |

## C syntax translations

```c
typedef struct LauncherContext { ... } LauncherContext;
```

Think `record LauncherContext` or a simple DTO. C has no constructors, so the
launcher initializes it with `memset` and then fills each field.

```c
static int FileExists(const wchar_t *path)
```

`static` at file scope is similar to a private method on an internal class. The
return value is `int` because C11 code commonly uses zero/nonzero instead of
`bool` at Win32 boundaries.

```c
void *opaque
```

This is the C equivalent of passing an `object state` to a callback and casting
it back to its real type inside the callback.

```c
if (!Operation()) return 0;
```

This project favors early returns instead of deeply nested exceptions. The
launcher writes a clear error before exiting; emulator backends write details to
the per-run log.

## Ownership and cleanup

C has no garbage collector. A function that opens/maps/allocates something has a
matching cleanup path:

- `CreateFileW` -> `CloseHandle`
- `CreateFileMappingW` -> `CloseHandle`
- `MapViewOfFile` -> `UnmapViewOfFile`
- `malloc` -> `free`

`ApkArchiveClose` is the equivalent of `Dispose()` and is safe to call after a
partially completed open.

## Where to start reading

1. `src/launcher/native_launcher.c` — heavily commented, small sequential workflow.
2. `src/shared/runtime_settings.c` — simple environment-backed configuration.
3. `src/backends/x86/main.c` — window, lifecycle and high-level boot sequence.
4. `src/backends/x86/loader.c` — ELF mapping/relocation.
5. Only then open the Dynarmic files; each combines an emulator, Android ABI and
   game-specific compatibility layer in one translation unit.

## About “binary-identical readability”

The readability pass does not rename or restructure established backend logic.
It adds this documentation and comments, which are not machine instructions.
Bug fixes such as the x86 log argument and ARMv7 startup correction necessarily
change the resulting binaries; a claim that those fixed binaries are identical
to the broken build would be false.
