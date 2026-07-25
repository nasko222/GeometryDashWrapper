# 0.9.4-arm-dynarmictest5

- Keeps the complete Test4 interactive Dynarmic x64 render/input/lifecycle loop.
- Adds a fatal guest-state block for `abort`, `exit`, `__stack_chk_fail`, `longjmp`, and `siglongjmp`.
- Dumps symbolized PC/LR, SP, CPSR, R0-R12, and a 160-byte stack window.
- Tracks the active native callback and nested guest call chain.
- Preserves and repeats the latest guest message-box text in the fatal block.
- Adds sequenced host input/lifecycle logging with coordinates, key values, text, and target guest addresses.
- Includes the recent JNI/import/input event ring in fatal diagnostics.
- Removes the duplicate fatal error line from the log without allowing execution to continue after an assertion.
- Renames build output, version metadata, window title, and result markers to DynarmicTest5.
- Uses the corrected full Test4 source package as its base and retains `game.apk`, all source, vendor dependencies, patches, and build helpers.
