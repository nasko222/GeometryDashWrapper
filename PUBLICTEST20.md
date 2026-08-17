# PublicTest20

PublicTest19 is a major functional milestone:
- the complete Forlorn 1.9c title scene begins rendering;
- PLAY opens SaveSelectScene;
- the old 0.101 build reaches its world map;
- realloc no longer kills SaveSelectScene.

The supplied screenshots/logs expose two remaining classes of failure.

## Non-power-of-two PVR textures

Forlorn 1.9c's title scene references several real RGBA4444 PVR textures whose
dimensions are deliberately non-power-of-two:

- FrostLevel_Sheet01.pvr.ccz = 768x1023
- particleImgSheet.pvr.ccz = 61x127
- FL_Map_World.pvr.ccz = 800x631

The logs show these resources opening/decompressing while no glTexImage2D
upload follows. FrostLevel_Sheet02 (1024x1024) does upload.

This explains several visible symptoms:
- torch-pillar sprites in LevelCave remain white/absent because they come from
  FrostLevel_Sheet01;
- particle imagery is incomplete;
- the newer 1.9c world-map screen can be almost completely black because
  FL_Map_World itself is NPOT.

PublicTest20 reports host NPOT capability to the old cocos2d CCConfiguration
path and allows those normal RGBA4444 textures through the already working
packed-pixel desktop OpenGL bridge.

## UIKit string texture path

The old 0.101 world-map screenshot is more complete, but its three map buttons
crash when activated. Immediately before the fault, the logs repeatedly show:

  sizeWithFont: -> zero rect
  drawInRect:withFont:... -> zero rect
  glTexImage2D ... 0x0

The same path also explains missing save-slot/map labels in both builds.

PublicTest20 adds a minimal legacy UIKit text bridge:
- UIFont systemFontOfSize:/boldSystemFontOfSize:/fontWithName:size:;
- NSString sizeWithFont: variants with non-zero CGSize;
- UIGraphicsPushContext/UIGraphicsPopContext tracking;
- drawInRect:/drawAtPoint: rasterization into the cocos2d bitmap context;
- Windows GDI text rasterization on the real Windows backend;
- a safe non-Windows fallback for source validation.

The goal is to remove zero-sized CCTexture2D text textures and allow the old
PlayLayer initialLoading path to continue beyond the exact PT19 fault.

PVRTC-compressed 4bpp textures (notably cave_bg_01/lightSheet) are still a
separate remaining renderer task; this build does not replace them with fake
art.
