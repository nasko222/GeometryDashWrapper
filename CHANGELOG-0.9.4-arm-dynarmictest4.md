# 0.9.4-arm-dynarmictest4

- Replaces the fixed 180-frame milestone loop with a persistent interactive render loop.
- Adds queued Win32 mouse, drag, Space/Up, Escape, text, and Backspace input forwarding.
- Resolves and calls `nativeTouchesBegin`, `nativeTouchesMove`, `nativeTouchesEnd`, `nativeKeyDown`, `nativeInsertText`, and `nativeDeleteBackward`.
- Adds Android `nativeOnPause`/`nativeOnResume` lifecycle forwarding for window activation and shutdown.
- Adds five-second FPS and average frame-time reporting in the log and window title.
- Keeps the Test3 Fix1 wall-clock execution guards and exact diagnostics.
- Produces a clean Dynarmic-only full-source tree with no old Bootstrap/PerformanceTest/Unicorn files.
- Adds a safe cleanup script for older working directories; build caches, APK, saves, and dist folders are preserved.
