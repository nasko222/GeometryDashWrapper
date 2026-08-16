# PublicTest2

PublicTest2 is the first iOS execution build.

The new `ios-armv7` backend accepts decrypted 32-bit ARM IPAs, selects an ARMv7
Mach-O slice, maps its segments, performs 32-bit dyld symbol binding and starts
the actual `LC_UNIXTHREAD` entry point in Dynarmic A32.

The bootstrap has a deliberately tiny Objective-C compatibility layer. Its
current finish line is `_UIApplicationMain`; it stops there and reports the
delegate it found. It does not implement UIKit's application loop, rendering or
audio yet.

Two supplied test binaries shaped this build:

- Geometry Dash 1.0: fat ARMv7/ARMv7s, unencrypted, legacy thread entry, C++ /
  cocos2d-x startup and 219 `__mod_init_func` entries.
- Forlorn 1.9c: thin ARMv7, unencrypted, legacy thread entry; its startup path is
  especially useful because it reaches `_UIApplicationMain` after a small
  Objective-C autorelease-pool setup.

A real ARMv7 dyld detail discovered while validating Geometry Dash 1.0 is that
some bind-stream address ULEBs intentionally wrap in 32-bit arithmetic. The
PublicTest2 binder therefore uses ARM32-width offset arithmetic rather than
incorrect 64-bit accumulation.

The existing Android execution paths are intentionally isolated from this work.
