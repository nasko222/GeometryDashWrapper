# Geometry Dash Wrapper 0.9.6-gdpstweaks6

Focused runtime correction over gdpstweaks5 based on the supplied 2019, 2022
and 2023 beta APKs plus the 2026-08-20 Windows logs.

## 2019 stock editor

The crash log entered `createObjectsFromSetup` after the wrapper's decompression
had failed. Static disassembly of the exact 9,144,004-byte stock library proves
that `PlayLayer::init` loads its `GJGameLevel*`, adds `0x110`, and passes that
std::string to `ZipUtils::decompressString(..., false, 11)`.

Tweaks6 therefore uses `GJGameLevel + 0x110` directly. The old largest-valid-
string heuristic is no longer used for this known profile. A failed compressed
payload is also never forwarded as if it were decoded level text; the recovery
path uses a safe empty setup instead of risking C++ string corruption.

## 2022 stock editor

The supplied 9,541,500-byte binary stores its `GJGameLevel*` at
`EditLevelLayer + 0x14C` (`str.w r10,[r4,#0x14c]`). Tweaks5 incorrectly reused
the 2023 `+0x150` member and rejected the real level object before editor init.
The three known profiles now use +0x140 (2019), +0x14C (2022), and +0x150
(2023) explicitly.

## FULL_BYPASS in late CreatorLayer

The lock block in the supplied 2022 binary compares r10 with zero; the supplied
2023/SubZero binary compares r9. Both otherwise have the same forward BEQ and
140-opacity/onOnlyFullVersion replacement block. The patch now matches the
register-agnostic Thumb-2 `cmp.w rN,#0` encoding and validates the tint block
before changing only that BEQ to an unconditional branch.

## Pause button

ARMv7 no longer assumes a pause CCMenuItem stays hidden forever after the first
`setVisible(false)`. The wrapper checks the node's Cocos visible byte and
re-applies hiding when the same item is made visible again during level setup,
restart, or other guest UI updates.

## Runtime status

Static/source checks are performed here. Windows/Dynarmic gameplay validation is
still required for the corrected 2019/2022 editor paths, SubZero CreatorLayer
FULL_BYPASS, and pause-button timing.
