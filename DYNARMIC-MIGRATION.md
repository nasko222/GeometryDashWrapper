# Dynarmic migration status

## Completed in DynarmicTest1

- 64-bit Windows build target
- pinned public Dynarmic GitLab mirror checkout with vendored dependencies
- ARMv5TE execution smoke test
- 32-bit sparse guest memory on a 64-bit host
- APK extraction
- authentic ARM ELF mapping and metadata verification

## DynarmicTest2

- ARM relocation application
- synthetic import/SVC gateway
- guest register and stack call ABI
- authentic constructors
- JNI_OnLoad

## DynarmicTest3

- nativeSetPaths and nativeInit
- JNI services and storage
- guest stdio and zlib
- Win32 OpenGL bridge
- bounded nativeRender loop and first graphical frame

## DynarmicTest4

- complete OpenGL bridge
- input and lifecycle
- gameplay benchmark against Unicorn

## Later

- audio
- remaining compatibility guards
- release-quality diagnostics and fallback behavior

## Milestone 2

DynarmicTest2 applies relocations, installs import/object traps, executes 238 constructors and calls JNI_OnLoad.

## Milestone 3

DynarmicTest3 connects the authentic ARM startup to Windows runtime services and attempts the first visible frame. Exact failure diagnostics are retained for any remaining bridge mismatch.


## DynarmicTest3 Fix1

The first Test3 Windows run passed relocation, all 238 constructors, `JNI_OnLoad`, `nativeSetPaths`, and Win32 OpenGL creation, then stopped only because the probe exhausted its artificial 500-million guest-tick allowance inside `nativeInit`.

Fix1 makes long startup calls use an unlimited guest-tick budget protected by a 120-second wall-clock guard. Each render call uses a 30-second wall guard. Five-second progress snapshots and terminal failures now include guest PC, LR, SP, CPSR, nearest dynamic symbol/ELF offset, recent import/JNI traps, and the busiest imports. Probe-only and first-frame runs use separate log files so one test cannot overwrite the other.
