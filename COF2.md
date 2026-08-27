# Geometry Dash Wrapper 0.9.7-cof2

COF2 is a narrow regression-fix branch over `0.9.7-cof1`. It does **not**
reintroduce the retired wrapper-owned stock 2.2 editor reconstruction.

## ARMv7 fixes

### Platformer keyboard in editor Play

COF1 retained a post-Endurance `GameManager` active-layer abstraction whose
runtime offset was initialized only by the stock-editor profile code that COF1
removed. The offset therefore remained zero and platformer keyboard commands
could not resolve the active game/editor-play layer.

COF2 restores the EnduranceTest10 behavior for the supported modded 2023 beta:
`GameManager + 0x168` is used directly by both the platformer input resolver and
the gameplay Edit callback bridge. The dead runtime offset member is removed.

### Background / ground selector OOB safety

The Endurance clamp itself was still present in COF1, but it was installed only
as a side effect of companion-editor visual hooks. COF2 makes the art safety
policy independent of editor capability. For the verified late-2023 primary
layout the primary `load/get` functions are always clamped to the packaged
Endurance limits of 18 ground slots and 26 background slots.

The patcher is idempotent so the validated companion visual bundle can safely
run afterward without failing its instruction validation.

## Preserved code

- x86 backend is byte-for-byte unchanged from COF1.
- shared audio implementation is byte-for-byte unchanged from COF1.
- ARM legacy behavior is unchanged except for the displayed COF2 version.
- the modded 2023 editor continues to use its validated packaged
  `LevelEditorLayerExt::initH`; no host-built stock editor is restored.
