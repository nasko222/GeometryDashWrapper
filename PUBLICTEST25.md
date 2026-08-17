# PublicTest25

PublicTest24 moved Forlorn 0.101 into a real PlayScene: the map/level scenery can
be entered and rendered, and a real Player object reaches scheduleUpdate. The
player sprite is still not visible and gameplay can hard-freeze after the scene
starts. Forlorn 1.9c still enters pathological level initialization and can fill
the 256 MiB guest heap.

PublicTest25 targets the shared runtime/state problems exposed by those runs:

- Correct the synthetic Objective-C guest-call boundary to Apple's 32-bit iOS
  PCS. r9 is caller-saved on iOS and is no longer restored. The preserved
  integer set is r4-r8, r10-r11 plus SP/LR return state.
- Keep ABI diagnostics for actual callee-saved corruption, but do not report or
  overwrite legal r9 changes.
- Implement NSString `rangeOfString:` / `rangeOfString:options:` as a real
  32-bit NSRange structure rather than the previous generic zero CGRect.
- Implement common NSString slicing methods used after range queries:
  `substringFromIndex:`, `substringToIndex:` and `substringWithRange:`.
- Implement `CGRectEqualToRect` and the ARMv7 structure-return ABI for
  `CGRectApplyAffineTransform`, including the transformed bounding rectangle.
- Improve NSAssertionHandler logging so `%@`-style first detail arguments such
  as missing sprite-frame names are surfaced when available.
- Add a gameplay host-call stall watchdog/heartbeat. A long Touch or Frame call
  now flushes PC, LR, guest call depth and the top nested method to the log.
  After a large safety budget it exits with `IOS_HOSTCALL_STALL` instead of
  hanging indefinitely with no useful tail.

The iOS heap remains 256 MiB. PublicTest25 does not increase it again: if 1.9c
still runs away, the log should be treated as a logic/runtime compatibility bug,
not a RAM requirement.
