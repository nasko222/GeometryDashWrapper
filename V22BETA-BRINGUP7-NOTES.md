# Geometry Dash 2.2 beta ARMv7 Bringup7

This branch changes two runtime boundaries only:

- Keeps the beta's original `ZipUtils::decompressString` and all guest C++ `std::string` ABI/lifetime behavior. The host replaces only `ccInflateMemory`, a C-style byte-buffer function.
- Adds **F2** as an emulator-only direct entry to the beta's real `CreatorLayer::onMyLevels` callback, bypassing the known device-specific editor button failure.

No APK is included. Build with an external APK.
