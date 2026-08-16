# PublicTest11

PublicTest10 proved real Forlorn artwork is rendering, but the Windows host
presented the iPhone's portrait 320x480 renderbuffer directly as a portrait
window. That is not how an old landscape iOS game was physically viewed.

PublicTest11:
- keeps guest rendering on a dedicated 480x720 portrait OpenGL framebuffer;
- rotates that framebuffer 90 degrees counter-clockwise for final presentation;
- starts with a resizable 960x540 (16:9) Windows client area;
- preserves Forlorn's native 3:2 landscape image, centered with black
  pillarboxes instead of stretching it to 16:9;
- recomputes aspect-fit presentation when the window is resized;
- leaves guest viewport/scissor/game coordinates untouched;
- retains the PublicTest10 IPA PNG/resource/affine fixes;
- remains open until manually closed.

Expected host line:
  IOS HOSTGL: Win32 OpenGL window ready client=960x540 logical=320x480
              offscreen=480x720 presentation=CCW90
              content=3:2 pillarboxed-in=16:9 ...
