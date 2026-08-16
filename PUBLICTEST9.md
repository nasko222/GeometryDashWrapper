# PublicTest9

PublicTest8 is the first build where Forlorn's real scene startup is stable:
BootScene is allocated, its BootLayer schedules a real selector, the scheduler
caches the correct guest IMP, `runWithScene:` executes, `onEnter` runs, and the
real BootScene visit/transform/draw path survives all 60 test frames.

PublicTest9 moves to the first visible-host experiment.

- Creates a real Win32 OpenGL window before Forlorn's EAGL/cocos2d setup.
- Forwards the fixed-function OpenGL ES 1.x calls used by Forlorn to desktop
  OpenGL where they map directly.
- Loads VBO entry points through WGL and translates guest data pointers.
- Collapses iOS framebuffer/renderbuffer objects onto the Win32 default
  framebuffer for this first test.
- `EAGLContext presentRenderbuffer:` now calls Win32 `SwapBuffers`.
- Keeps a 60 Hz host message/pacing loop for up to 600 frames (~10 seconds).
- Observes `runWithScene:` so the final diagnostic reports BootScene rather than
  the pre-first-frame nil runningScene query.
- Because desktop OpenGL normally cannot decode iOS PVRTC textures, compressed
  texture uploads are temporarily replaced with a visible magenta/white 4x4
  checkerboard. This is diagnostic only; real PVR/PNG resource decoding is a
  later step.
- A tiny placeholder CoreGraphics/UIImage path lets image-backed textures reach
  the GL upload code instead of immediately becoming nil.

Expected result:
  RESULT: IOS_HOST_OPENGL_PROBE_OK frames=600 ... running-scene-class=BootScene

The window may show incomplete/placeholder art. PublicTest9 is specifically a
proof that real Forlorn draw calls can reach a visible Windows OpenGL surface.
Input and real iOS texture decoding are not implemented yet.

Geometry Dash 1.0 still intentionally defers AppController until its 219
Mach-O static constructors are implemented.
