# DynarmicTest4

## Goal

Turn the proven Dynarmic first-frame renderer into an interactive wrapper that remains open until the user closes it.

## Runtime behavior

The Win32 procedure queues input and lifecycle events. Guest ARM calls are dispatched only from the main emulation thread between `nativeRender` calls, avoiding reentrant Dynarmic execution from inside a Windows callback.

Touch coordinates are scaled from the current client area to the fixed native game resolution. Mouse-move events are coalesced so a large Windows message burst cannot create an unbounded guest-input backlog.

The render loop pauses while the window is inactive and calls the authentic ARM lifecycle exports when available. Every five seconds it writes FPS and average frame time to `gd-dynarmic-interactive.log` and updates the window title.

## Expected terminal milestones

```text
RESULT: DYNARMIC_NATIVE_INIT_RETURNED
RESULT: DYNARMIC_INPUT_BRIDGE_READY
RESULT: DYNARMIC_RENDER_LOOP_ENTERED
RESULT: DYNARMIC_FIRST_FRAME_OK
```

After the window is closed cleanly:

```text
RESULT: DYNARMIC_BRINGUP4_OK
```

## Logs

Interactive run:

```text
gd-dynarmic-interactive.log
```

Constructor/JNI regression run:

```text
gd-dynarmic-probe-only.log
```
