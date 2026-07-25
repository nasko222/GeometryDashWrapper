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
- first graphical frame

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
