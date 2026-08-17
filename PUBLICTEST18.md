# PublicTest18

PublicTest17 completed the real cocos2d touch chain:
- CCMenu ccTouchBegan
- CGRectContainsPoint HIT
- CCMenuItem selected
- targeted touch claimed
- CCMenu ccTouchEnded
- CCMenuItem unselected
- CCMenuItem activate

Yet neither Forlorn 1.9c nor 0.101 entered MenuScene onPlay/onContinue.

The exact remaining action mechanism is Foundation NSInvocation. The embedded
cocos2d CCMenuItem implementation stores an `NSInvocation *invocation_`.
`initWithTarget:selector:` obtains `[target methodSignatureForSelector:action]`,
creates `[NSInvocation invocationWithMethodSignature:]`, sets target/selector,
and for one-object actions stores the CCMenuItem sender at invocation argument
index 2. `activate` then simply sends `[invocation_ invoke]`.

PublicTest17 had no NSMethodSignature or NSInvocation runtime support, so
methodSignatureForSelector returned nil, invocation_ remained nil, and the real
activate method became an Objective-C no-op.

PublicTest18 adds:
- guest instance/class methodSignatureForSelector:;
- fake NSMethodSignature numberOfArguments and basic metadata;
- +[NSInvocation invocationWithMethodSignature:];
- setTarget:/target;
- setSelector:/selector;
- setArgument:atIndex:/getArgument:atIndex:;
- retainArguments/argumentsRetained;
- invoke/invokeWithTarget:;
- real guest tail dispatch from NSInvocation into MenuScene onPlay:/onContinue:;
- dedicated IOS INVOCATION and IOS MENU ACTION diagnostics.

PublicTest17's full-logo CoreGraphics fix, performSelector:withObject:withObject:,
NSMutableSet targeted touches, CGRect hit tests, PVR/plist/stdIO handling and
landscape host presentation are retained.
