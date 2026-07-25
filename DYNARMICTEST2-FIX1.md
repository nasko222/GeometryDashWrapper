# DynarmicTest2 fix 1

The original DynarmicTest2 used `ticks_left = 0` inside `CallSVC` as an indirect request to return from `Jit::Run()`. That is not a synchronous callback boundary. A translated block can continue beyond the SVC before the host import dispatcher writes the return registers.

Fix 1:

- attaches the callback environment to the active `Dynarmic::A32::Jit`;
- calls `HaltExecution(UserDefined1)` for SVCs, exceptions, interpreter fallback and invalid memory;
- calls `ClearHalt(UserDefined1)` immediately after `Run()` returns and before dispatching imports;
- enables `check_halt_on_memory_access`;
- preserves nested `pthread_once` guest execution;
- prints the complete executor failure to the console as well as the log;
- reports PC, LR, SP, CPSR and halt reason for unexplained stops.

Build:

```cmd
BUILD_DYNARMIC_X64.cmd
```

Output:

```text
dist-arm-wrapper-dynarmictest2-fix1
```

Expected successful end markers remain:

```text
RESULT: DYNARMIC_CONSTRUCTORS_OK count=238
RESULT: DYNARMIC_JNI_ONLOAD_OK result=0x00010004
RESULT: DYNARMIC_BRINGUP2_OK
```
