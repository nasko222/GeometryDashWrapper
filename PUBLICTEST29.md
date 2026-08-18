# Geometry Dash Wrapper 0.9.6-publictest29

PublicTest29 targets the common no-frame crash that remained after PT28.

## What the PT28 logs proved

PT28's narrow Everyplay bypass did execute, but it only covered `Everyplay` / `EveryplayFeatures` entry points. Geometry Dash then immediately entered other classes from the same obsolete SDK.

Observed common path:

- GD 1.91: `EveryplayAudioManager` interruption listener path -> fault `address=0x3ffc pc=0x4000`
- GD 1.90: same audio-manager path -> fault `address=0x3ffc pc=0x4000`
- GD 2.11: same audio-manager path -> fault `address=0x3ffc pc=0x4000`
- GD 1.81: `EveryplaySoundEngine` / OpenAL capability path -> fault `address=0x3ffc pc=0x4000`

This happens before the first cocos2d frame is presented.

## PublicTest29 changes

### Entire Everyplay SDK quarantined

Any Objective-C class whose name begins with `Everyplay` is now treated as an unavailable third-party service. The wrapper does not enter its guest IMPs at all.

- singleton/constructor-style class calls return a stable inert object
- capability queries and service calls return unavailable / zero
- instance setters/getters are harmless no-ops
- retain/init-style identity calls preserve the inert receiver

Expected markers:

```
IOS EVERYPLAY QUARANTINE: class=EveryplayAudioManager selector=+sharedInstance ...
IOS EVERYPLAY QUARANTINE: fake class=EveryplayAudioManager selector=...
```

The point is to prevent unsupported recording/telemetry code from running before Geometry Dash itself starts.

### Legacy OpenAL dynamic symbol lookup

GD 1.81 performs:

```
dlsym(..., "alcMacOSXMixerOutputRate")
```

PT28 returned NULL. PT29 returns a callable synthetic import stub for that obsolete Apple OpenAL extension. Calling it is a successful no-op, because host audio owns the real mixer timing.

Expected marker:

```
IOS DYNSYM: dlsym 'alcMacOSXMixerOutputRate' -> synthetic stub 0x...
```

### Darwin ctype imports

`__tolower` / `__toupper` (and non-underscored aliases) now return real ASCII/ctype transformations instead of the generic zero import stub. This also removes one known blocker in the 1.0 startup path.

## Test priority

1. Geometry Dash 1.91
2. Geometry Dash 2.11
3. Geometry Dash 1.90 / 1.81
4. Geometry Dash 1.0 separately

A successful PT29 test should contain no `objc guest ... Everyplay... dispatch` lines after the quarantine begins. If the game still closes, the next `RESULT:` should be outside the Everyplay SDK and will be the next real Geometry Dash/cocos2d compatibility target.
