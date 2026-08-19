# Source contents

Complete Geometry Dash Wrapper 0.9.6-publictest35 source: Android x86/ARM
backends, native launcher, IPA/Info.plist/Mach-O analyzer, new ARMv7 iOS
Mach-O/Dynarmic bootstrap backend, PowerShell/CMD build scripts, pinned
dependency metadata, zlib/stb source, icon families and documentation.

APKs, IPAs, extracted game executables and built binaries are not included.

PublicTest26 iOS focus: Geometry Dash ARMv7 startup via LC_MAIN/LC_UNIXTHREAD plus dyld-style __mod_init_func constructor execution.

PublicTest30 iOS focus: bypass obsolete Everyplay telemetry, implement Darwin errno/ARC helpers, and continue Geometry Dash startup past the shared PT27 AppController crash.

PublicTest30 additions:
- PUBLICTEST29.md - full Everyplay quarantine + legacy OpenAL dlsym notes.
- PUBLICTEST29-VERIFICATION.txt - static verification and PT28 failure summary.

PublicTest31 adds iOS ARMv7 core runtime fixes for compiler division helpers,
C++ RTTI/dynamic_cast, old libstdc++ string mutation, guest printf formatting,
Foundation search paths, and binary-safe logging. No game/IPAs are included.

- `PUBLICTEST32.md` — NSData/UIImage byte-backed PNG bridge for cocos2d image loading.

PublicTest32 adds byte-backed NSData and UIImage imageWithData decoding so cocos2d PNG file reads can become real CGImage/CCTexture2D pixel data instead of nil textures.


## PublicTest33
Adds CCDirectorCaller-based cocos2d-x frame pumping and Win32 ES2 shader/program forwarding for GD 2.11/SubZero.

## PublicTest34
Adds real libc numeric/token/BMFont parsing (`atoi`, `strtol`, `strtoul`, `strtok`, `sscanf`) plus precise read/write stack diagnostics and frame-present traces for the shared GD 2.11/SubZero objectDefinitions failure.

## PublicTest35
Adds guest-visible `glMapBufferOES`/`glUnmapBufferOES` VBO shadow mapping with host upload on unmap, resets GLSL/active-texture state for the wrapper's fixed-function presentation pass, and logs offscreen non-black pixel counts plus the last 44-byte C++ allocation at a fault.
