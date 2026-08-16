# PublicTest8

PublicTest7 proved the recursive class dispatch fix: Forlorn now creates the
real CCDirectorDisplayLink subclass and enters its real frame loop. Frame 2,
however, jumped into the Mach-O header page (PC 0x1004) while cocos2d scheduler
callbacks were active.

The primary PublicTest8 fix is Objective-C runtime IMP lookup:
- `methodForSelector:` on real guest instances returns the actual inherited IMP.
- class/instance `respondsToSelector:` uses the real method hierarchy.
- common `performSelector...` calls immediately dispatch real guest methods.
- scene/scheduler messages have dedicated tracing so BootScene/runWithScene and
  callback IMP lookup are visible even after the normal dispatch log cap.

Additional runtime correctness:
- `objc_getProperty` / `objc_setProperty*` guest ivar access.
- successful no-op objc synchronization / SJLJ registration helpers.
- persistent UIApplication singleton/delegate.
- basic UIDevice and NSThread values.
- NSString C-string construction and prefix/suffix/path helpers.
- real ARM soft-float math results for observed cocos2d math imports.

No host rendering window is added yet. The goal is stable real BootScene and
scheduler execution before translating OpenGL ES to Windows.

Geometry Dash 1.0 remains deferred until its 219 Mach-O static constructors are
implemented.
