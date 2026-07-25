# Geometry Dash ARM Wrapper 0.9.4-arm-dynarmictest1

## What this milestone is

DynarmicTest1 begins the 64-bit Windows migration and proves the replacement CPU backend can be built and connected to the real Geometry Dash 1.4 ARM library.

This milestone is a **bring-up probe**, not yet a playable wrapper. It intentionally stops before running the real constructors because the existing Android import/JNI/OpenGL bridge still has to be moved from Unicorn hooks to Dynarmic traps.

## Why x64

Dynarmic generates code only for 64-bit host architectures. The Android guest remains 32-bit ARMv5TE and keeps the same 32-bit guest addresses.

## Build

Run:

```text
BUILD_DYNARMIC_X64.cmd
```

The builder checks out the public Dynarmic mirror on GitLab at the pinned revision:

```text
a41c380246d3d9f9874f0f792d234dc0cc17c180
```

That revision contains the vendored dependencies and the v6.7.0-compatible A32 API. Git for Windows is required only for this public checkout; GitHub authorization is neither requested nor needed. The builder explicitly disables Credential Manager and terminal authentication prompts. No administrator access or system-wide C/C++ toolchain is required. Compilation still uses portable Zig 0.14.1, CMake 3.31.10, and Ninja 1.13.2, targeting:

```text
x86_64-windows-gnu
```

The first public checkout can be large because the mirror contains fmt, mcl, robin-map, xbyak, Zydis, and its other required source dependencies. Later builds reuse `.build-tools\dynarmic-gitlab-a41c380246d3-src`. Use `BUILD_DYNARMIC_X64.cmd -RefreshDynarmic` only if that checkout becomes incomplete. An incomplete directory left by the retired GitHub URL is removed automatically.

Output:

```text
dist-arm-wrapper-dynarmictest1
```

Run:

```text
dist-arm-wrapper-dynarmictest1\RUN_DYNARMIC_PROBE.cmd
```

## Expected successful output

```text
Host pointer bits: 64
RESULT: DYNARMIC_X64_THUMB_SMOKE_OK r0=8 guest=v5TE host=x86_64
Extracted lib/armeabi/libgame.so: 4549112 bytes
Authentic ARM constructors: 238
Exports: JNI_OnLoad=0x100fed81 nativeInit=0x100fecf9 nativeRender=0x101e840d
RESULT: DYNARMIC_APK_ELF_OK constructors=238 exports=ready memory=ready
RESULT: DYNARMIC_BRINGUP1_OK
```

The complete output is saved to `gd-dynarmic-probe.log`.

## What the probe validates

- The produced executable is genuinely 64-bit.
- Dynarmic executes a Thumb ARMv5TE instruction sequence correctly.
- Sparse 32-bit guest memory callbacks work at high guest addresses.
- `game.apk` is parsed without external tools.
- `lib/armeabi/libgame.so` is extracted and CRC checked.
- The ARM ELF image is validated and mapped at `0x10000000`.
- PT_LOAD segments, dynamic symbols, relocations, `.init_array`, and required JNI exports are found.
- The exact library has 238 constructors before the next milestone attempts to execute them.

## Next milestone

DynarmicTest2 will port the runtime boundary:

1. Apply all ARM relocations.
2. Replace Unicorn import hook ranges with a Dynarmic SVC/trap gateway.
3. Port register/stack call setup.
4. Execute all 238 constructors.
5. Execute `JNI_OnLoad`.
6. Compare constructor and JNI execution speed against Unicorn.

Only after that succeeds should nativeInit, OpenGL, input, audio, and gameplay rendering be connected.

## Files added

- `BUILD_DYNARMIC_X64.cmd`
- `build-dynarmic-x64.ps1`
- `dynarmic-x64/CMakeLists.txt`
- `tools/dynarmic-x64-zig.cmake`
- `tools/zigar-x64.cmd`
- `tools/zigranlib-x64.cmd`
- `src/dynarmic_probe.cpp`

## Builder hotfix 3

If the build stops in `fmt/ostream.h` with `__std_stream file not found`, apply
builder hotfix 3. The corrected builder patches the cached vendored fmt source
idempotently and automatically rebuilds the stale CMake cache.
