# V22 Beta NetworkTest3 — hard wall-clock worker preemption

NetworkTest3 keeps NetworkTest2 as the complete baseline and changes only the
cooperative CCHttpClient worker scheduler plus branch/build metadata.

## Freeze identified from NetworkTest2

The captured request progressed through two normal frame slices. The third slice
resumed immediately after the guest `memset` import (`pc=0x210000cc`, return
address `0x106c5f54`) and never returned to the frame pump. This means the
2 ms slice limit could not be checked: it was outside the call to
`Dynarmic::A32::Jit::Run()`, while the translated guest block remained active.

## NetworkTest3 change

- Retains NetworkTest2 asynchronous DNS, nonblocking sockets, numeric
  `getnameinfo`, condition wakeups, and 2 ms / 12-run frame slicing.
- Adds a 4 ms wall-clock watchdog to every guest worker JIT run.
- The watchdog calls Dynarmic `HaltExecution(UserDefined1)` from a helper thread,
  saves the exact guest registers, and resumes the worker on a later frame.
- A watchdog preemption is a normal timeslice yield, not a worker failure.
- Adds PC/LR and watchdog counters to the focused network diagnostics.

## Preservation policy

All non-network source files come byte-for-byte from NetworkTest2. In particular,
local saves, editor bridges, companion-library policy, platformer input/rendering,
audio, APK caching, level recovery, keyboard behavior, and lifecycle handling are
not replaced by DynarmicTest14. DynarmicTest14 is used as the known-good reference
for the original CCHttpClient worker ABI and socket/import behavior.

Build with `BUILD_V22BETA_X64.cmd`, then run `RUN_NETWORKTEST3.cmd`.
The focused log is `gd-networktest3.log`.
