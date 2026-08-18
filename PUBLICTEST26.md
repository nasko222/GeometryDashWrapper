# Geometry Dash Wrapper 0.9.6-publictest26

PublicTest26 switches the iOS ARMv7 work back to Geometry Dash as the primary bootstrap target.

## iOS startup changes

- Adds `LC_MAIN` support for newer 32-bit iOS executables.
  - The runtime converts Mach-O `entryoff` into the guest virtual PC using the `__TEXT` segment mapping.
  - `LC_MAIN` receives `argc`, `argv`, `envp`, and `apple` in the ARM argument registers.
  - Legacy builds still use `LC_UNIXTHREAD` and their kernel-style initial stack.
- Parses the real `__mod_init_func` table instead of recording only its size.
- Executes every static constructor before entering the app entry point, matching dyld ordering.
- Constructor execution has dedicated logging and fault diagnostics with constructor index and target PC.
- Adds the minimum C++ runtime primitives required by Geometry Dash constructors:
  - `operator new` / `new[]`
  - `operator delete` / `delete[]`
  - nothrow new/delete variants used by later builds
  - `__cxa_atexit`
  - `__cxa_guard_acquire`, `__cxa_guard_release`, `__cxa_guard_abort`

## IPA startup matrix verified from supplied test files

| Build | Entry form | Runtime entry | Constructors |
|---|---|---:|---:|
| Geometry Dash 1.81 | `LC_UNIXTHREAD` | `0x9350` | 261 |
| Geometry Dash 1.90 | `LC_UNIXTHREAD` | `0xA5B0` | 251 |
| Geometry Dash 1.91 | `LC_UNIXTHREAD` | `0x8C10` | 252 |
| Geometry Dash 2.11 | `LC_MAIN` entryoff `0x1C8D91` | `0x1CCD91` | 305 |
| Geometry Dash SubZero 1.0 | `LC_MAIN` entryoff `0x1E5165` | `0x1E9165` | 311 |

All constructor pointers in these five ARMv7 slices were verified to land inside executable `__TEXT`; all are Thumb entry points.

## Expected log progression

Legacy Geometry Dash should now show `IOS DYLD: running ... static constructors`, complete them, then enter the `LC_UNIXTHREAD` startup path and reach `UIApplicationMain`.

Geometry Dash 2.11 and SubZero should no longer fail with `LC_UNIXTHREAD ARM entry PC was not found`. They should run constructors and then start from `source=LC_MAIN`.

If a constructor fails, PublicTest26 reports `IOS_CONSTRUCTOR_*` with the constructor index and target so the next missing runtime primitive can be fixed directly.

Android backends are intentionally unchanged by the iOS bootstrap work.
