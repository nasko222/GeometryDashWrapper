# PublicTest15

PublicTest14 reaches the actual Forlorn MenuScene and remains stable. The user's
latest run reached 1778 frames and terminated only when the host window was
closed. Two concrete menu bugs remained.

## Dead menu input

The Win32 mouse -> UIKit bridge is working: touchesBegan/Moved/Ended arrive at
the real EAGLView. cocos2d then transforms each touch into menu coordinates and
calls CoreGraphics `CGRectContainsPoint` for CCMenu hit testing.

PublicTest14 returned zero for every `_CGRectContainsPoint` import. Every menu
item therefore behaved as if the pointer was outside its rectangle.

PublicTest15 implements the ARMv7 by-value CGRect/CGPoint ABI and real
CGRectContainsPoint. CGRectIntersectsRect and CGRectContainsRect are included
for the same geometry path.

## Magenta / missing atlas art

Forlorn's MenuSheet.pvr.ccz decompresses correctly to a 256x512, 16-bit
RGBA4444 PVR texture. PublicTest14 forwarded packed 16-bit texture uploads
through a generic host pointer path. PublicTest15 explicitly recognizes
RGBA4444, RGBA5551 and RGB565, validates the correct two-byte-per-pixel guest
span, converts them to RGBA8, and uploads the converted host buffer.

This removes driver/ABI ambiguity from the exact MenuSheet upload.

## Remaining resource path holes

Old cocos2d also constructs atlas image names using NSString
`stringWithString:` and `stringByAppendingPathComponent:`. Those were still
stubs in PublicTest14 and caused resources such as UISheet to become 2x2
fallback textures. PublicTest15 implements those path operations, plus
pathExtension/stringByDeletingPathExtension and a minimal assertion handler.

The 960x540 wrapper presentation remains unchanged in this test. The native
Forlorn game surface is still rendered at 320x480 and presented as undistorted
480x320 landscape content.
