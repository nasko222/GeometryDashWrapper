# PublicTest2 checklist

Android regression:
- launch one x86 APK;
- launch one legacy ARM APK;
- launch the ARMv7 2.2 beta;
- confirm networking, saves, audio and editor behavior did not regress.

iOS bootstrap:
- drag the decrypted Geometry Dash 1.0 IPA onto either `RUN_AUTO_*.cmd`;
- drag Forlorn 1.9c onto either launcher;
- send the newest `logs/.../ios-armv7.log` for each;
- success for this build means reaching
  `RESULT: IOS_BOOTSTRAP_REACHED_UIAPPLICATIONMAIN`;
- a window/gameplay is not expected yet in PublicTest2.
