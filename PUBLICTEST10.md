# PublicTest10

PublicTest9 successfully opened a real Win32 OpenGL window, ran the real
Forlorn BootScene for 600 frames, presented 600 frames, and exited only because
the diagnostic build intentionally stopped after ten seconds. The window was
black.

The PublicTest9 log and source exposed two concrete causes:

1. `_CGAffineTransformIdentity` was incorrectly treated as a function import.
   The game therefore read the ARM SVC thunk bytes (first word 0xef000001) as
   the first float of the identity transform. All subsequent CoreGraphics node
   transforms were garbage.
2. UIImage/CoreGraphics were still reporting every image as a 2x2 placeholder.
   Forlorn's real `Default.png` boot artwork is 320x480, so the boot sprite was
   effectively reduced to a few pixels.

PublicTest10 fixes those paths:
- real guest data constants for CGAffineTransformIdentity, CGPointZero,
  CGSizeZero and CGRectZero;
- real 32-bit iOS soft-float CGAffineTransform Make/Translate/Scale/Rotate/
  Concat/Invert handlers;
- indexes resources directly inside Payload/Forlorn.app without extracting or
  redistributing them;
- resolves NSBundle resource paths and preserves UIImage resource identity;
- decodes ordinary and Apple CgBI PNG RGBA data at runtime with zlib;
- uses real PNG width/height and pixel data in the CoreGraphics texture path;
- runs the host window until the user closes it instead of auto-closing after
  ten seconds;
- removes the per-frame BootScene visit/transform/draw console flood while
  keeping scene lifecycle diagnostics.

No game assets are included in this source package. They are read only from the
IPA supplied at runtime.
