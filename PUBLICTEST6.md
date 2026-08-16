# PublicTest6

Forlorn 1.9c reached and returned from its real AppDelegate in PublicTest5.

PublicTest6 moves from bootstrap into the application/frame-loop layer:

- Parse Objective-C categories from `__objc_catlist` and merge their real
  instance/class methods into guest classes. This fixes cocos2d category methods
  such as `+[CCDirector setDirectorType:]` and CCTexture2D pixel/PVR helpers.
- Search guest superclass chains for inherited Objective-C methods.
- After AppDelegate returns, call the real `+[CCDirector sharedDirector]`.
- Query the real `-runningScene`.
- Drive the real inherited `-drawScene` method for 60 synthetic 60 Hz frames.
- Implement deterministic `gettimeofday`, `time`, rand/random and usleep timing
  sufficient for cocos2d frame delta calculations.
- Recognize mapped constant NSString/CFString-like objects for common string
  operations that previously became nil stubs.

This is still a diagnostic rendering stage: OpenGL ES calls remain bootstrap
stubs and no Win32 window/input bridge is claimed yet. The goal of this build is
to prove that the actual Forlorn scene/scheduler/render loop can execute.

Geometry Dash 1.0 continues to defer AppController launch until its 219 Mach-O
static constructors are implemented.
