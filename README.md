# Geometry Dash Wrapper 0.9.6 — PublicTest2

PublicTest2 keeps the existing Android APK wrapper path and adds the first real
32-bit iOS execution bootstrap.

## APKs

Drag an APK onto `RUN_AUTO_GDPS.cmd` or `RUN_AUTO_BOOMLINGS.cmd` exactly as
before. The x86, legacy ARM and ARMv7 Android runtime paths are carried forward
from PublicTest1/EnduranceTest12 apart from the displayed version string.

## IPAs — new in PublicTest2

Drag a decrypted 32-bit ARM `.ipa` onto either launcher. The native launcher
first prints the existing Info.plist/Mach-O analysis, then starts the separate
`ios-armv7/RobTopIOSArmV7.exe` backend.

The iOS backend selects the ARMv7 slice, maps the Mach-O segments at their
preferred addresses, processes dyld bind/weak/lazy-bind information, prepares a
Darwin-style initial stack and executes the real `LC_UNIXTHREAD` ARM entry point
with Dynarmic A32.

This is a bootstrap milestone, not an iOS emulator yet. The minimal Objective-C
compatibility layer is intentionally only large enough to test startup. On
success the backend stops at `_UIApplicationMain` and logs:

`RESULT: IOS_BOOTSTRAP_REACHED_UIAPPLICATIONMAIN`

UIKit event handling, rendering, audio and actual gameplay are future work.
PublicTest2 does not bypass App Store encryption and refuses encrypted binaries.

The bootstrap was designed against user-supplied decrypted copies of Geometry
Dash 1.0 and Forlorn 1.9c. No IPA or extracted Apple executable is included in
this source archive.
