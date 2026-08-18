Geometry Dash Wrapper 0.9.6-publictest25 -> publictest26 source hotfix

Apply these files over a clean PublicTest25 source tree, or use the supplied .patch.

PublicTest26 focuses on Geometry Dash iOS startup:
- LC_MAIN entry support for newer ARMv7 IPAs
- real __mod_init_func constructor execution before app entry
- C++ new/delete and __cxa guard/atexit primitives needed during constructor startup
- constructor-specific crash diagnostics

No game/IPA payload is included.
