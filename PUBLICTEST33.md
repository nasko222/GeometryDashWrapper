# Geometry Dash Wrapper 0.9.6-publictest33

PublicTest33 targets the first real frame of Geometry Dash 2.11 and Geometry Dash SubZero after PublicTest32 successfully completed their iOS AppController launch and texture initialization.

## Changes

- Adds a cocos2d-x 2.x frame bridge using the real Objective-C `CCDirectorCaller` shim when no Objective-C `CCDirector` class exists.
- Acquires `+sharedDirectorCaller` dynamically from parsed Objective-C metadata.
- Drives the real guest `-doCaller:` callback once per host frame. The guest method enters the C++ cocos2d director main loop via its own vtable.
- Keeps the older Objective-C `CCDirector +sharedDirector / -drawScene` frame path for older games that expose it.
- Adds real Win32 OpenGL forwarding for the ES2 shader/program calls observed in GD 2.11/SubZero: shader/program creation, source upload, compilation, linking, use, uniform/attribute lookup, vertex attributes and VAOs.
- Removes ES precision qualifiers (`lowp`, `mediump`, `highp`) from guest shader source before compiling it in the desktop OpenGL compatibility context.
- Virtualizes both core and OES FBO/renderbuffer names while retaining the wrapper-owned portrait framebuffer.
- PublicTest32 NSData/UIImage PNG decoding remains unchanged.

## Expected PT33 markers

For GD 2.11/SubZero, after `real delegate launch returned r0=0x1`, expect:

```
IOS FRAME: CCDirector ObjC class absent; using CCDirectorCaller display-link shim
IOS FRAME: acquired object=0x... class=CCDirectorCaller bridge=CCDirectorCaller
IOS: frame pump begin frame=1 bridge=CCDirectorCaller class=CCDirectorCaller selector=doCaller: imp=0x...
```

During shader bootstrap, successful host objects should no longer be zero:

```
IOS HOSTGL ES2: glCreateProgram -> <non-zero>
IOS HOSTGL ES2: glCreateShader -> <non-zero>
```

If a shader cannot compile on the host driver, PT33 logs the real compile/link error.

## Scope

GD 1.0 and GD 1.91 still have independent guest crashes before delegate return in the PT32 logs. PT33 intentionally does not hide those with nil-pointer workarounds; this test focuses on the two builds already reaching a successful delegate return.
