# 0.9.4-arm-dynarmictest3

- Executes authentic ARM `nativeSetPaths` through Dynarmic x64.
- Adds JNI runtime services required by Cocos2d-x and Geometry Dash startup.
- Adds Windows save/preference access and Android path translation.
- Adds guest stdio/file proxies for direct APK reads.
- Adds 32-bit guest zlib stream bridging.
- Adds Win32 OpenGL window/context creation and x64-safe GL argument marshalling.
- Executes `nativeInit` and enters a bounded `nativeRender` loop.
- Adds first-frame and probe-only launchers.
- Preserves exact import/JNI/GL failure diagnostics for iterative bring-up.
