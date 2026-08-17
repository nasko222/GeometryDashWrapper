# PublicTest14

PublicTest13 no longer crashes. Forlorn reaches the real MenuScene, replaces
BootScene, accepts mouse/touch input, and stays alive until the user closes the
window. The visible menu, however, contains only direct PNG artwork such as
`forlorn_logo_menu.png`.

The exact missing layer is Foundation property-list/container behavior.
Forlorn's menu buttons and much of its UI are TexturePacker sprite frames
described by binary `MenuSheet.plist`. The PVR/CCZ texture file already opens
and decompresses in PublicTest13, but the wrapper still returned empty fake
NSDictionary/NSMutableDictionary objects, so cocos2d could not construct
CCSpriteFrame entries.

PublicTest14 adds:
- generic Apple `bplist00` parser for dictionaries, arrays, strings, booleans,
  integers and reals;
- `+[NSDictionary dictionaryWithContentsOfFile:]` backed by IPA resources;
- persistent NSDictionary/NSMutableDictionary objectForKey/setObject behavior;
- persistent NSArray/NSMutableArray mutation behavior;
- Objective-C fast enumeration `countByEnumeratingWithState:objects:count:`;
- NSEnumerator nextObject;
- NSNumber numeric accessors and constructors;
- CGRectFromString / CGSizeFromString / CGPointFromString using the 32-bit
  iOS hidden-struct-return ABI;
- NSLock lock/unlock no-op behavior for cocos2d texture/frame caches;
- texture-upload diagnostics independent of the normal import log cap.

The actual MenuSheet.plist in the supplied Forlorn 1.9c IPA is binary format
and contains 9 TexturePacker frames plus metadata. No game assets are included
in this source package.
