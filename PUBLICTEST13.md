# PublicTest13

PublicTest12 fixed the Forlorn 1.9c loading thread. The real `loadManagers`
method now completes, `loadingFinished` becomes true, and BootLayer immediately
starts constructing `MenuScene`.

The first MenuScene load then failed at `MenuSheet.pvr.ccz`: cocos2d resolved
the asset correctly but called the C `fopen` API. PublicTest12 returned 0 for
that import, and the real cocos2d CCZ/PVR loader dereferenced the null FILE*.
The two independent 1.9c PublicTest12 runs fail at the same PC/LR immediately
after that null fopen.

PublicTest13 adds:
- read-only IPA-backed fopen/fread/fseek/ftell/rewind/feof/fclose;
- gzip-backed gzopen/gzread/gzclose/gzeof;
- guest-buffer zlib uncompress, used by cocos2d CCZ texture loading;
- real asset path resolution through the existing NSBundle/IPA index;
- correctly sized checker fallback for genuinely unsupported compressed GL
  texture formats instead of collapsing them to a 4x4 texture.

The supplied Forlorn 1.9c `MenuSheet.pvr.ccz` was validated offline: it is a
CCZ v1 zlib stream, 36,971 bytes compressed -> 262,196 bytes decompressed. Its
PVR payload is 256x512, 16-bpp RGBA4444, so after file I/O + CCZ inflation it
should use the existing normal `glTexImage2D` path rather than the compressed
texture fallback.

Older-build note: Forlorn 0.101 already runs a real MenuScene for hundreds of
frames under PublicTest12 and receives mouse/touch input. Forlorn 1.02 is a
separate loader case: it reports `imports bound=0` and needs pre-dyld-info
symbol/indirect-table binding; this build intentionally does not mix that
loader work into the 1.9c MenuScene fix.
