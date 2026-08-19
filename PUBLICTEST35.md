# Geometry Dash Wrapper 0.9.6-publictest35

PublicTest35 targets the first *visible rendering* blocker after PublicTest34 proved that Geometry Dash 2.11 can execute and present multiple real cocos2d frames while the Windows window remains black.

## Changes

- Implements `glMapBufferOES` for guest code using a guest-visible shadow allocation sized from the currently bound VBO's preceding `glBufferData` call.
- Implements `glUnmapBufferOES` by uploading the modified shadow bytes into the real host OpenGL VBO with `glBufferSubData`.
- Implements `glGetBufferParameterivOES(GL_BUFFER_SIZE)` from the tracked VBO state.
- Tracks and releases VBO shadow allocations when host buffers are resized or deleted.
- Fixes the wrapper's final portrait-to-landscape presentation pass so it explicitly uses GLSL program 0 and texture unit 0 while drawing the legacy fixed-function blit quad, then restores the guest program/texture-unit state.
- Adds an early offscreen framebuffer probe reporting how many source pixels are non-black before the final Windows blit. This separates an empty guest frame from a broken presentation pass.
- Retains the PT34 low-address diagnostics and adds the most recent 44-byte C++ allocation to fault reports for the separate post-frame objectDefinitions crash.

## Why

PT34 proved that GD 2.11 completed and presented eight host frames before its later `0x18` write fault. In frame 1, however, the game called `glMapBufferOES(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES)` and `glUnmapBufferOES`; both were still generic zero-return stubs. cocos2d therefore received a null mapping and never populated the dynamic VBO used by the following indexed draw.

PT33 also introduced real ES2 shader forwarding. That means a guest GLSL program can remain bound when `HostOpenGLWindow::Present()` performs its old fixed-function `glBegin` presentation quad. PublicTest35 explicitly switches to program 0 for that host-only blit.

## Expected markers

On GD 2.11/SubZero frame 1:

```
IOS HOSTGL VBO MAP: target=0x8892 buffer=1 access=0x88b9 size=3648 guest=0x...
IOS HOSTGL VBO UNMAP: target=0x8892 buffer=1 size=3648 guest=0x... uploaded=1
```

Before each of the first few host presents:

```
IOS HOSTGL SURFACE: present=... nonblack-rgb=.../153600 max-rgb=... current-program=...
```

Interpretation:

- `nonblack-rgb > 0` means the guest produced visible pixels and any remaining all-black Windows output is in the final host blit.
- `nonblack-rgb = 0` means the guest frame itself is still empty, so the next target is upstream render state/draw data.

If the separate objectDefinitions crash remains, the fault report also includes:

```
IOS CXX LAST NEW44: ptr=0x... caller=0x... sp=0x...
```

## Scope

GD 2.11 and SubZero are the primary PT35 tests because PT34 already proved their delegate, texture decode/upload, director caller, and multi-frame execution paths. GD 1.91 still has a separate pre-frame libstdc++ object/string fault and is not the primary target of this test.
