# Geometry Dash Wrapper 0.9.6-publictest27

PublicTest27 focuses on opening real Geometry Dash iOS ARMv7 builds after PT26 completed Mach-O startup and static constructors but crashed inside AppController before the first cocos2d frame.

## iOS ARMv7 runtime changes

- Implements 32-bit legacy libstdc++ `std::string` storage used by Geometry Dash 1.0/1.81/1.90/1.91, including the copy/C-string constructors, assignment, append, push_back, erase, substr, find/compare helpers and several mutation helpers.
- Implements the 32-bit libc++ `std::__1::basic_string` layout used by Geometry Dash 2.11 and SubZero. Host-created values use the ABI-compatible long representation.
- Implements core libc operations that PT26 incorrectly sent through the generic zero-return import stub: `strcpy`, `strncpy`, `strcmp`, `strncmp`, case-insensitive comparisons/search, `strchr`, `strrchr`, `strstr`, `strpbrk`, `strlcat`, `memcmp`, `memchr`, and `memset_pattern16`.
- Implements `dispatch_once` enough for Geometry Dash singleton initialization: the once token is tracked and the 32-bit guest block invoke function is executed as a nested ARM call.
- Handles the later Objective-C ARC spelling `objc_retainAutoreleaseReturnValue` as an identity operation instead of returning nil.
- Accepts both observed spellings of the SjLj register/unregister imports.
- Memory-fault diagnostics now capture the PC/LR at the actual invalid memory callback (`fault-pc` / `fault-lr`) in addition to the post-halt CPU state.

## Why this build exists

PT26 proved that all tested GD IPAs can run all static constructors, enter their Mach-O entry point, reach `UIApplicationMain`, create the host OpenGL window, and enter the real AppController delegate. The next common blocker was missing C/C++ runtime behavior before any visible cocos2d frame could be presented. PT27 targets that layer directly.

## Test priority

1. Geometry Dash 1.91
2. Geometry Dash 1.81 / 1.90
3. Geometry Dash 2.11
4. Geometry Dash SubZero 1.0

Look for `IOS CXXSTR:` and `IOS DISPATCH:` lines. On any remaining memory fault, report the new `fault-pc` / `fault-lr` fields.
