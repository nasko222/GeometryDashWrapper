# PublicTest4

PublicTest4 advances the ARMv7 iOS compatibility path using the real supplied
Forlorn 1.9c IPA.

Changes:
- Bypass the bundled legacy TestFlight SDK's `+[TestFlight takeOff:]` bootstrap.
  TestFlight telemetry/cache/network startup is not required for Forlorn gameplay.
- Continue executing the real Forlorn `AppDelegate` immediately after that call.
- Report delegate tick-budget exhaustion as `IOS_DELEGATE_TICK_BUDGET_EXHAUSTED`
  instead of falling back to the generic UIApplicationMain success result.
- Geometry Dash 1.0 remains intentionally deferred at UIApplicationMain because
  its 219 Mach-O static constructors are not implemented yet.
- Existing Android APK behavior and the established 2.2 fixes are not changed.

Expected Forlorn progression includes:

    IOS: bypassing legacy TestFlight +takeOff: ... policy=telemetry-disabled

After that, the next real UIKit/OpenGL/Foundation call reached by AppDelegate
becomes the next compatibility target.
