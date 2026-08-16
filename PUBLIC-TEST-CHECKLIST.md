# PublicTest1 checklist

APK regression:
- launch an x86 APK;
- launch a legacy ARM APK;
- launch the ARMv7 2.2 beta;
- confirm networking, saves and editor behavior are unchanged.

IPA analyzer:
- drag several RobTop IPAs onto either `RUN_AUTO_*.cmd`;
- send the complete console output;
- include at least one old 32-bit IPA if available;
- note whether the analyzer reports ARMv7, ARM64, a fat binary, or encryption;
- report any IPA that says no root bundle, no executable or not Mach-O.
