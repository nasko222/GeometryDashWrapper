# PublicTest7

PublicTest6 successfully drove 60 real cocos2d `drawScene` frames, but the
director's running scene remained nil.

The Forlorn runtime log identified the exact reason:
`+[BootScene node]` fell through to a bootstrap stub. BootScene inherits from
CCScene, which inherits from CCNode, and `+[CCNode node]` exists in the real
Mach-O. PublicTest6 already contained a recursive class-method resolver, but
normal `objc_msgSend` class dispatch accidentally still used exact-class
`FindMethod()`.

PublicTest7 changes that single dispatch path to `FindClassMethodRecursive()`.

Expected progression:
- `BootScene +node` dispatches to the inherited real `+[CCNode node]` IMP.
- Because Objective-C preserves the original class receiver, `self` remains
  BootScene and the inherited factory should allocate a BootScene instance.
- AppDelegate can then pass the real BootScene into cocos2d.
- The post-launch runningScene query should become non-nil.
- The existing 60-frame real `drawScene` probe remains unchanged.

No host rendering window is added yet. Android paths are unchanged except for
version strings.
