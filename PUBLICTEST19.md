# PublicTest19

PublicTest18 proves that the PLAY action itself is now working.

Forlorn 1.9c reaches:
  CCMenuItemSprite -activate
  NSInvocation invoke MenuScene -onPlay:
  SaveSelectScene construction

Forlorn 0.101 reaches the same next-scene path.

## Exact PLAY crash

All supplied PublicTest18 runs then fail at the same libc call:

    realloc(old_pointer, 0x20)

PublicTest18 did not implement realloc, so the generic import fallback returned
0. The following cocos2d allocation/assertion path then dereferenced address
0x8.

PublicTest19 implements malloc-size tracking plus realloc and reallocf:
- realloc(NULL,n) behaves as malloc;
- realloc(p,0) returns NULL and releases size tracking;
- normal realloc allocates a new guest block and copies min(old,new) bytes;
- free removes tracked size metadata;
- diagnostics report old/new size and copied bytes.

## Missing title-screen scenery

The visible Forlorn logo and PLAY button are not the complete MenuScene.

Static inspection of Forlorn 1.9c shows:
  MenuSceneBackground -getDictForLevelType: IMP 0x76999
  calls CCFileUtils fullPathFromRelativePath:
  with the embedded CFString "LevelCave.plist"
  then allocates Foundation NSDictionary
  then sends initWithContentsOfFile:.

PublicTest18 supported +[NSDictionary dictionaryWithContentsOfFile:] but did
not support -[NSDictionary initWithContentsOfFile:]. The generic fake-object
initializer therefore returned an empty dictionary.

That explains why MenuSceneBackground, three CCParallaxNodes and particle
containers existed while their level-described cave/background objects were
missing.

PublicTest19 implements:
- NSDictionary/NSMutableDictionary initWithContentsOfFile:;
- initWithDictionary:;
- dictionaryWithDictionary:;
- NSArray/NSMutableArray initWithArray:;
- explicit IOS PLIST INIT diagnostics.

The full-logo CoreGraphics fix, MenuSheet, NSInvocation, targeted touches,
CGRect hit testing, packed PVR uploads, binary plist parser, IPA stdio and
Android isolation are retained.
