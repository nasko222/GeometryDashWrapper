# Geometry Dash Wrapper 0.9.6 — PublicTest1

PublicTest1 keeps the existing Android APK wrapper path and adds the first
read-only iOS IPA inspection path.

## APKs

Drag an APK onto `RUN_AUTO_GDPS.cmd` or `RUN_AUTO_BOOMLINGS.cmd` exactly as
before. The x86, legacy ARM and ARMv7 backend selection and runtime paths are
otherwise carried forward from EnduranceTest12.

## IPAs — new in PublicTest1

Drag an `.ipa` onto either launcher. PublicTest1 does **not** execute iOS code
yet. It opens the IPA as a ZIP, locates `Payload/<app>.app/Info.plist`, reads XML
or binary plist metadata, locates the app executable, and inspects thin/fat
Mach-O headers. It reports the CPU architecture, entry-point style, imported
dylibs/frameworks and the Mach-O encryption flag.

ARMv7/ARMv7s is reported as the best future target because the wrapper already
has an A32 Dynarmic execution core. ARM64 is identified but still needs a new
AArch64 execution path. PublicTest1 never attempts to bypass an encrypted iOS
executable.

Build with `BUILD_ALL.cmd`. APKs, IPAs and built binaries are intentionally not
included in the source archive.
