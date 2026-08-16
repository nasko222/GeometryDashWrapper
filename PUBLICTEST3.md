# PublicTest3

PublicTest3 advances the ARMv7 iOS backend past `_UIApplicationMain` for Mach-O
apps that do not require static C/C++ constructors before launch.

Forlorn 1.9c is the primary probe target. It has zero `__mod_init_func` entries,
so the wrapper now creates its real `AppDelegate` instance and executes the
guest `applicationDidFinishLaunching:` IMP in Dynarmic. Objective-C messages to
classes implemented inside the Mach-O are tail-dispatched back into their real
guest IMPs; basic UIKit/Foundation objects remain conservative host stubs.

Geometry Dash 1.0 reaches `_UIApplicationMain`, but has 219 Mach-O static
constructors. PublicTest3 intentionally defers its delegate launch rather than
running AppController with uninitialized C++ state. It also validates the
UIApplicationMain delegate name against real guest classes, eliminating the
spurious `@%` diagnostic from PublicTest2.

This is still a bootstrap/probe build. A successful Forlorn result means its
real delegate launch method returned; it does not yet imply a rendered window or
playable game. Android execution behavior is unchanged apart from the displayed
0.9.6-publictest3 version.
