# PublicTest16

PublicTest15's latest log proves the Win32 mouse -> UIKit -> cocos2d hit-test
path reaches a real HIT on a 153x67 menu item. The item still never activates.

Forlorn embeds cocos2d's CCTargetedTouchHandler with a claimedTouches ivar of
type NSMutableSet. Targeted touch delegates add a UITouch after a successful
ccTouchBegan and only receive moved/ended/cancelled while that touch remains in
the claimed set. PublicTest15 did not implement NSMutableSet, so a mouse-down
could HIT while mouse-up was never delivered to CCMenu ccTouchEnded.

PublicTest16 adds backed NSSet/NSMutableSet constructors and
addObject/removeObject/containsObject behavior, plus dedicated claim logging.

It also fixes the remaining fake-NSString path hole: PublicTest15 still logged
stringByAppendingPathComponent: as a stub during UISheet loading. Path methods
now work directly on fake NSString objects even when their current base string
is empty. componentsSeparatedByString: is included.

Dedicated traces now expose CCMenu touch methods, CCMenuItem activate, and
MenuScene onPlay:/onContinue: independently of the normal Objective-C log cap.

All PublicTest15 packed-texture, CGRectContainsPoint, binary-plist, IPA stdio,
loading-thread, input, and landscape work is retained.
