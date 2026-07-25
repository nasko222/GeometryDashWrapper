# 0.9.4-arm-dynarmictest2-fix1

- Replaced tick-counter-only SVC stopping with explicit `Dynarmic::A32::Jit::HaltExecution`.
- Clears the callback halt before host import dispatch and nested guest calls.
- Enabled halt checks on guest memory accesses.
- Added console-visible execution errors and register diagnostics.
- Kept the 277 function traps, 7 imported objects, 238 constructors and JNI_OnLoad milestone unchanged.
