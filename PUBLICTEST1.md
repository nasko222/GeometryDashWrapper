# PublicTest1

## Goal

Start iOS support without destabilizing the working Android wrapper.

## Added

- `.ipa` detection in the native drag-and-drop launcher.
- ZIP discovery of the root `Payload/<app>.app` bundle.
- XML and binary `Info.plist` readers for bundle ID, name, version/build and
  `CFBundleExecutable`.
- Thin and fat Mach-O inspection.
- ARMv6/ARMv7/ARMv7s/ARM64/x86/x86_64 identification.
- `LC_MAIN` entry-offset reporting, imported dylib/framework listing and
  `LC_ENCRYPTION_INFO` / `LC_ENCRYPTION_INFO_64` status.
- An explicit execution assessment: A32 can reuse the existing Dynarmic CPU
  core; ARM64 needs a future AArch64 path.

## Deliberately not implemented yet

- Mach-O mapping/relocations/binding.
- Objective-C runtime, UIKit/Foundation/CoreFoundation compatibility.
- iOS OpenGL/audio/filesystem lifecycle shims.
- iOS game execution.
- Any attempt to bypass App Store encryption.

The APK path branches before the new IPA analyzer and otherwise remains on the
existing EnduranceTest12 launcher/runtime flow.
