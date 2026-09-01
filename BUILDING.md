# Building Geometry Dash Wrapper 0.9.7-cof5 on Windows

On a 64-bit Windows machine, extract the source and run:

```bat
BUILD_ALL.cmd
```

This builds the x86 backend, legacy ARM Dynarmic backend, ARMv7 Dynarmic
backend, and native launcher into:

```text
dist-unified\
```

The ARM builder revision for this branch is
`dynarmic-x64-builder108-0.9.7-cof5`.

The launch scripts default to `FPS=VSYNC`. Change that to a numeric value such
as `144`, `240`, or `9999` for an uncoupled numeric host frame cap.

COF5 contains no Geometry Dash 1.02 comments hotkey implementation.
