# DynarmicTest5

## Goal

Capture enough guest state to identify the callers behind the two Test4 aborts: the menu-browsing crash and the `CCDictionary::setObject` assertion reached while opening Stereo Madness. Test5 does not bypass or ignore guest assertions.

## Runtime behavior

The interactive render loop, queued Win32 input bridge, coordinate scaling, mouse-move coalescing, lifecycle forwarding, and five-second FPS reporting remain unchanged from Test4.

Every dispatched input/lifecycle callback is now written to `gd-dynarmic-interactive.log` with a sequence number, guest address, and relevant values.

## Fatal diagnostic block

A guest call to `abort`, `exit`, `__stack_chk_fail`, `longjmp`, or `siglongjmp` writes a block beginning with:

```text
===== DYNARMIC TEST5 GUEST FATAL DIAGNOSTIC =====
```

The block contains:

- fatal import name
- active `RunFunction` call chain, such as `nativeRender frame N` or `nativeTouchesEnd`
- symbolized PC and LR, including nearest ELF symbol and `ELF+0x...` offset
- SP, CPSR, and R0-R12
- the last guest `showMessageBox` title/body, when one occurred
- the recent host/JNI/import event ring
- 32 bytes before SP and 128 bytes after SP, with guest code pointers symbolized

The detailed block is followed by one final `ERROR: guest called fatal import ...` line and `RESULT: DYNARMIC_BRINGUP5_FAILED`. The duplicate fatal error line from Test4 is removed.

## Expected terminal milestones

```text
RESULT: DYNARMIC_NATIVE_INIT_RETURNED
RESULT: DYNARMIC_INPUT_BRIDGE_READY
RESULT: DYNARMIC_RENDER_LOOP_ENTERED
RESULT: DYNARMIC_FIRST_FRAME_OK
```

After a clean close:

```text
RESULT: DYNARMIC_BRINGUP5_OK
```

## Test procedure

1. Run once and browse the same menu path until it crashes.
2. Save that log.
3. Run again and open Stereo Madness until it crashes.
4. Save that log separately.

The LR and symbolized stack candidates should reveal whether both aborts share the same caller.
