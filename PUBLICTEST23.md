# PublicTest23

PublicTest23 targets the two concrete blockers reported by the PT22 runtime logs.

## Forlorn 0.101

PT22 proved the reusable allocator is active, but the 128 MiB iOS guest heap still reaches its exact end at `0x38000000` while the level is constructing assets and containers. PT23 expands only the isolated iOS guest heap to 256 MiB (`0x30000000`-`0x3fffffff`). The import, fake-object and stack regions remain unchanged. Allocation-failure logging now includes live bytes/blocks and reuse count.

## Forlorn 1.9c

PT22's fault dump identifies a deterministic low-address read in `PlayLayer -addSpriteSheets`. The method saves `self` in ARM callee-saved register r4, but the failing run reaches an ivar load with r4 equal to zero. PT23 tracks the real PlayLayer instance used as the synchronous NSThread `initialLoading` target and adds a deliberately narrow invariant guard around `-addSpriteSheets`: if r0 at method entry or r4 while executing/returning to that method becomes zero, it restores the tracked PlayLayer pointer and emits an `IOS PLAYLAYER SELF REPAIR` line. Boundary diagnostics record PC/LR/r0/r4 so a remaining fault can be attributed to the exact nested call.

This is not a global nil-object workaround. The repair applies only to the real guest PlayLayer instance and only to `-addSpriteSheets`.
