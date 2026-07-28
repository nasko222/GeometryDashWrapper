# Unified7 Fix3 — regression repair

This build keeps the recovered CMD/EXE layout, dated per-run logging and drag-an-APK-to-RUN_AUTO.cmd workflow.

Changes:
- Removes Fix2's synthetic x86 API-connect and account-URL rewrite experiments. They could break networking on versions that had worked previously.
- Adds desktop keyboard offset suppression for exported keyboardWillShow/keyboardWillHide/forceOffset/textInputShouldOffset callbacks.
- Removes the ARMv7 companion ShaderFix-only execution path. Directly sanitizes GLES precision qualifiers before compiling shaders on desktop OpenGL and records shader/program compiler errors.
- Stops rewriting ARMv7 editor viewport, scissor and framebuffer clear state. The game's rendering state is guest-owned again, avoiding host-side loss of narrow overlays such as the editor song line.
- Keeps the Geometry Dash 1.0.0 forced-HD exclusion, Lite gauntlet safety, icon improvements, flat saves and all previously retained working fixes.
- Keeps dated logs and drag-and-drop APK launching.
