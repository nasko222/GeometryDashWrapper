# Geometry Dash Wrapper 0.9.6-gdpstweaks15

Tweaks15 is deliberately narrower than tweaks12–14. The user confirmed the x86 key issue is fixed, so x86 is frozen. The confirmed 1.0 audio implementation is also frozen. This pass addresses only two failures demonstrated by the tweaks14 ARMv7 logs.

## 2023 2.2 beta: crash when pressing Play

The restored 2023 editor now reaches normal editor runtime, but pressing Play faults in `cocos2d::ccArrayDoubleCapacity` immediately before its `realloc` return store. The faulting call has a zero old capacity and a null element pointer.

Exact 9,578,364-byte binary disassembly proves:

- `CCArray::initWithCapacity` stores the native `ccArray*` at object `+0x30`.
- `CCArray::init()` passes capacity `1`.
- `ccArrayNew(0)` itself normalizes zero to capacity `1`.
- the late native `ccArray` stores capacity at `+0x04` and its element array at `+0x0C`.
- `ccArrayDoubleCapacity` doubles that capacity and reallocates the element array.

Therefore a zero-capacity CCArray shell is not a legitimate initialized collection. Tweaks14 accepted that shell because it only rejected `count > capacity`; when `count == capacity == 0`, the object passed validation. Tweaks15 rejects capacity zero and rebuilds the field through the real `CCArray::create()` path before EditorUI/Play uses it. The same validation remains profile-aware: 2019 uses object `+0x20` / elements `+0x08`, while 2022/2023 use object `+0x30` / elements `+0x0C`.

## 2019 2.2-family beta: editor button appears to freeze

The 2019 run does not hit a single immediate fault. It enters restored EditorUI construction and then spends the remainder of the run resolving missing art. The log contains 1,236 unique `DYNARMIC_V22_EDITOR_MISSING_SPRITE_FRAME` entries and climbs beyond 3.6 million guest imports before the run is terminated.

Those 1,236 names were compared against the frame keys in the exact 55,405,908-byte APK's packaged sprite-sheet plists. All 1,236 are absent from the APK. Tweaks15 builds the same packaged-frame index at runtime for the Early2019 profile. During the narrow `EditorUI::create()` window:

- names present in the APK index still execute the untouched stock `spriteFrameByName` lookup;
- names proven absent return the existing known-good fallback frame before entering the expensive guest dictionary/string lookup;
- a stock lookup that still returns null retains the old fallback behavior.

The early fast-fallback thunk was independently assembled as Thumb code and its literal-load opcodes were checked against Clang/LLVM output.

Tweaks15 also stops flushing the log file once per missing frame. It logs the first 32 unique names and periodic summaries instead. This avoids thousands of synchronous disk flushes during the already-expensive editor construction.

## Explicitly unchanged

- x86 backend and editor hotkey behavior from tweaks14.
- Shared Windows audio source and the confirmed 1.0 music fix.
- Preview Mode implementation from tweaks13/14. Preview is not changed again until the editor/Play paths are stable enough to test it independently.
- APK-derived background/ground limits and the dual GameManager layer-pointer work from earlier passes.
