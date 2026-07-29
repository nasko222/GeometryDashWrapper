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


## Endurance branch examples

### Configuration switch

```c
const int isolated =
    GetBooleanSetting(L"VERSION_ISOLATED_SAVES", 1);
```

This is equivalent to `configuration.GetValue<bool>(..., true)` in .NET or a
boolean system property with a default in Java. The BAT files supply the value;
the native launcher owns the decision.

### Blocking only one UI hit target

The pause option does not monkey-patch `UILayer::onPause`. Each window host
checks whether a mouse-down is inside the small upper-right gameplay rectangle.
Only that one touch is discarded. Escape still reaches the game's normal
Android Back handler and opens the real pause menu.

### Frame deadline

The x86 `pace_x86_frame` helper is a tiny fixed-step scheduler. Think of a
`Stopwatch`/`System.nanoTime()` deadline loop: the game supplies its desired
animation interval through JNI, `SwapBuffers` supplies vblank synchronization,
and QPC fills only the remaining time. The render loop temporarily requests a
1 ms Windows timer period so its coarse Sleep does not overshoot by a full
default scheduler quantum.

### ARMv7 exact editor path

The late beta ships an optional companion `libgame.so` with a complete
`LevelEditorLayerExt::updateVisibilityH` routine. The wrapper validates its
symbol, executable range and original-function slot before redirecting the
primary editor's calls. Conceptually this resembles assigning a delegate to a
known interface slot and then calling the extension implementation.

The older `HostV22EditorVisibility` adapter remains as the fallback selected by
`V22_EXACT_EDITOR_VISIBILITY=false`.

### Legacy HTTP 100 Continue

Some old account clients send large backups in two phases: headers first, body
after an `HTTP/1.1 100 Continue` reply. The x86 runtime keeps the partial request
in a descriptor-keyed state table, emits the interim reply, then joins the body
before sending the complete request through WinHTTP. This is similar to keeping
a per-connection request builder in a C# dictionary, except C uses a fixed array
and an SRW lock.
