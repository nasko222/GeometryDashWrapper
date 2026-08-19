# Geometry Dash Wrapper 0.9.6-publictest32

PublicTest32 targets the first common post-launch rendering failure seen in iOS Geometry Dash 1.90, 1.91 and SubZero after PublicTest31.

## What the PT31 logs proved

The games now pass Mach-O startup, static constructors, UIApplicationMain, AppController setup, third-party-service quarantine, local data loading and cocos2d resource lookup. They successfully open `GJ_LaunchSheet(.png/-hd.png)` and `game_bg_01_001(.png/-hd.png)`, but the texture factory returns nil and the next guest call faults through a null sprite/texture object.

Static ARM analysis of Geometry Dash 1.91 shows the cocos2d PNG path is:

1. read the PNG bytes from the IPA;
2. `+[NSData dataWithBytes:length:]`;
3. `+[UIImage imageWithData:]`;
4. `-[UIImage CGImage]`;
5. `CGImageGetWidth/Height/...` and `CGBitmapContextCreate` / `CGContextDrawImage`;
6. upload the decoded pixels into `CCTexture2D`.

PublicTest31 only created a generic dummy NSData object and did not implement `UIImage +imageWithData:`. The UIImage call therefore returned nil even though the file read succeeded.

## PT32 changes

- `NSData +dataWithBytes:length:` now copies the real guest bytes into a host-side NSData backing store.
- `dataWithBytesNoCopy:length:` and `dataWithBytesNoCopy:length:freeWhenDone:` use the same safe copied backing store.
- NSData `length` and `bytes` are implemented for the fake Foundation object.
- `UIImage +imageWithData:` now decodes the supplied bytes with the existing iOS PNG/CgBI decoder.
- decoded byte-backed UIImage objects are inserted into the existing decoded-image cache using a synthetic key.
- `UIImage -CGImage`, `CGImageGetWidth/Height`, bitmap drawing and the existing OpenGL texture bridge can therefore consume the real decoded pixels without inventing a filename.
- `DecodeAsset` checks its decoded-image cache before normal IPA-path normalization so byte-backed UIImage/CGImage objects remain resolvable.

## New diagnostics

A successful path should include lines similar to:

```
IOS FOUNDATION DATA: dataWithBytes ... length=... copied=...
IOS ASSET: UIImage imageWithData decoded 512x512 RGBA ...
```

For Geometry Dash 1.91 the first large background is expected to decode as 1024x1024. Geometry Dash 1.90 and SubZero use a 512x512 `game_bg_01_001.png` in the supplied test IPAs.

This is a real pixel-data bridge; PT32 does not force the texture factory to return success and does not substitute a placeholder image.
