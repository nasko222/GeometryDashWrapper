# PublicTest5

Forlorn 1.9c is the primary iOS target.

PublicTest5 fixes the false execution-budget exhaustion discovered from the
PublicTest4 runtime log. Dynarmic import/SVC halts now charge only ticks that
actually executed rather than an entire 5,000,000-tick chunk per import.

It also adds the small UIKit/EAGL bootstrap values already observed in Forlorn:
- Objective-C struct-return dispatch and 320x480 UIScreen/view CGRects.
- EAGLView/UIView `layer` -> CAEAGLLayer.
- Foundation convenience objects such as NSNumber/dictionaries/arrays/URLs.
- EAGLContext success behavior needed during surface setup.
- Minimal framebuffer/OpenGL ES/OpenAL query/object-generation stubs.

These graphics stubs are bootstrap-only: PublicTest5 does not claim visible
rendering yet. Their purpose is to let the real cocos2d startup continue until
the next genuine compatibility boundary.

The TestFlight telemetry bypass from PublicTest4 remains.
Geometry Dash 1.0 still defers AppController launch until its 219 Mach-O static
constructors are implemented.
