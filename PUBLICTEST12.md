# PublicTest12

PublicTest11 rendered Forlorn's real launch artwork but remained permanently on
BootScene and did not accept mouse input.

The exact splash blocker is now understood from the real Forlorn Mach-O:
`applicationDidFinishLaunching:` allocates NSThread, calls
`initWithTarget:self selector:@selector(loadManagers) object:nil`, then `start`.
PublicTest11 did not implement NSThread start. Therefore the real
`-[AppDelegate loadManagers]` never executed and its final
`loadingFinished_ = 1` write never happened. `-[BootLayer checkLoaded:]` waits
for that flag forever.

PublicTest12:
- implements NSThread initWithTarget:selector:object:;
- dispatches NSThread start to the actual guest target IMP synchronously;
- implements detachNewThreadSelector:toTarget:withObject: similarly;
- does not force the loadingFinished flag: the real loadManagers code sets it;
- maps Win32 left mouse down/move/up/cancel to UIKit touch phases;
- maps mouse coordinates through the 3:2 pillarbox and inverse CCW90 rotation;
- sends events through the real guest EAGLView touchesBegan/Moved/Ended/
  Cancelled methods and therefore through the game's real CCTouchDispatcher;
- supplies minimal UITouch, UIEvent, NSSet and NSArray behavior;
- uses an exact 320x480 offscreen iOS framebuffer and leaves upscaling to final
  16:9-window presentation, removing the unnecessary internal 1.5x scale.

Expected markers:
  IOS THREAD: NSThread start selector=loadManagers target=AppDelegate policy=synchronous
  IOS SCENE: ... selector=loadingFinished ...
  IOS INPUT: touchesBegan:withEvent: guest=(...)
  IOS INPUT: touchesEnded:withEvent: guest=(...)
