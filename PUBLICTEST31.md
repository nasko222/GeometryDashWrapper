# Geometry Dash Wrapper 0.9.6-publictest31

PublicTest31 is a core iOS ARMv7 runtime correctness pass based on the PT30 Geometry Dash logs.

## Why PT30 still showed a black window
PT30 successfully passed Mach-O startup, static constructors, UIApplicationMain, the AppController delegate, and the Everyplay/dlsym blockers. The next failure is in ordinary cocos2d/C++ startup rather than an optional SDK.

The PT30 logs exposed four core runtime operations that were still incorrectly stubbed to zero:
- `___dynamic_cast` returned NULL for live C++ objects.
- `___udivsi3` returned zero instead of performing unsigned division.
- old libstdc++ `std::string::_M_mutate` returned without changing the string.
- `sprintf` / `vsnprintf` returned zero without writing their destination buffer.

They also showed `NSSearchPathForDirectoriesInDomains` returning nil, leaving cocos2d without a valid Documents/Library path.

These mistakes explain both the final NULL-object virtual-call faults and the binary garbage that appeared in pathname log messages.

## PT31 changes
- optimistic live-object `__dynamic_cast` bridge for the startup casts observed in GD
- `__udivsi3`, `__umodsi3`, `__divsi3`, `__modsi3`
- old libstdc++ `std::string::_M_mutate`
- ARMv7 guest-format support for `sprintf`, `snprintf`, and `vsnprintf`
- Objective-C `NSString +stringWithFormat:`
- `NSSearchPathForDirectoriesInDomains` with stable sandbox-style Documents/Library/Caches paths
- C++ ABI RTTI `_ZTVN10__cxxabiv1*`, `_ZTI*`, and `_ZTS*` are treated as DATA rather than executable imports (important for GD 1.0)
- binary/unprintable strings are escaped and length-capped in pathname/URL logs instead of being written raw

## New useful diagnostics
Look for:
- `IOS CXX RTTI: __dynamic_cast ...`
- `IOS CXXSTR: libstdc++ _M_mutate ...`
- `IOS FOUNDATION PATH: NSSearchPath ...`
- `IOS LIBC FORMAT: ...`

A bad pathname will now look like `\x89PNG\r\n\x1a\n...` instead of corrupting the text log with raw binary bytes.
