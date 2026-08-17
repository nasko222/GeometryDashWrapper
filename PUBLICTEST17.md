# PublicTest17

PublicTest16 finally renders the real PLAY button and the latest logs prove the
button is hit correctly:

- CCMenu receives ccTouchBegan.
- CGRectContainsPoint returns HIT for the 153x67 button.
- CCMenuItemSprite enters selected state.
- CCTargetedTouchHandler claims the UITouch in NSMutableSet.
- touchesEnded sees that same claimed touch and removes it.

The remaining click failure is now exact. Cocos2d's targeted touch dispatcher
uses NSObject's three-argument convenience method:

    performSelector:withObject:withObject:

for moved/ended/cancelled callbacks. The wrapper implemented only
performSelector: and performSelector:withObject:. The dispatcher therefore
removed the claimed touch without ever entering CCMenu ccTouchEnded, so
CCMenuItem activate and MenuScene onPlay: could never run.

PublicTest17 implements the two-object performSelector form and tail-dispatches
the real guest method with both Objective-C object arguments.

The "resolution is cut off" visual bug also has a concrete source. For direct
PNG textures, cocos2d often allocates a power-of-two backing bitmap (for
example 512x256 for the 334x142 Forlorn menu logo). PublicTest10-16 ignored the
CGRect passed to CGContextDrawImage and stretched the 334x142 source across the
entire 512x256 backing bitmap. Cocos2d then sampled only the original content
rectangle, showing only the upper-left portion of the stretched image.

PublicTest17 now honors CGContextDrawImage's CGRect, draws the image only into
that content rectangle, and leaves the power-of-two padding transparent.

Expected click chain:
  IOS INPUT HITTEST: ... -> HIT
  IOS INPUT CLAIM: NSMutableSet addObject ...
  IOS PERFORM2: CCMenu -> ccTouchEnded:withEvent: ...
  IOS MENU: CCMenu -ccTouchEnded:withEvent: ...
  IOS MENU: CCMenuItemSprite -activate ...
  IOS MENU: MenuScene -onPlay: ...

Expected direct-PNG diagnostic:
  IOS CG: DrawImage context=512x256 rect=(0,0,334,142) source=334x142 padding=transparent
