# PublicTest24

PublicTest23 moved both supplied Forlorn builds past their previous blockers, but
its PlayLayer-specific r4 repair was at the wrong abstraction layer.

Runtime evidence from PublicTest23:
- Forlorn 1.9c triggered the r4 repair 33 times while PlayLayer -addSpriteSheets
  was active, then entered runaway allocation and filled the 256 MiB guest heap.
- Forlorn 0.101 no longer hit the old heap limit; it reached a new low-address
  fault with corrupted-looking callee-saved state while its live heap was only
  about 9 MiB.
- The recurring 1.9c corruption is seen immediately after nested real guest
  Objective-C IMPs. Those IMP calls were previously implemented as raw tail
  jumps from the objc_msgSend SVC bridge.

PublicTest24 replaces that behavior with an explicit ARM call boundary for real
guest Objective-C methods. Before entering a guest IMP, the wrapper saves AAPCS
callee-saved r4-r11, SP, and the original LR. The guest method returns through a
synthetic ARM SVC trampoline; the wrapper then restores the saved state and
resumes at the original Thumb/ARM return address while preserving r0 as the
method return value. Nested Objective-C calls use a stack of these frames.

This applies to normal objc_msgSend, stret guest dispatch, objc_msgSendSuper2,
performSelector, NSInvocation, and the synchronous NSThread compatibility path.
Top-level delegate/director/frame/touch callbacks retain their existing host
return trampoline.

The PublicTest23 PlayLayer r4/r0 repair has been removed completely. PT24 also
logs IOS ABI REPAIR only when a guest IMP actually returns with changed
callee-saved registers or SP, and adds fault selector/callsite/call-stack
context if a later crash remains.

The 256 MiB isolated iOS guest arena is retained for now, but PT24 does not
increase it further. A runaway allocator after this ABI correction should be
treated as a real lifecycle/loop bug rather than hidden with more memory.
