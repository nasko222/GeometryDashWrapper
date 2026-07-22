# Building GD Wrapper 0.9.4-arm-bootstrap13

The ARM backend is built as a 32-bit Windows executable and links Unicorn
statically. The runtime folder therefore needs no Unicorn DLL. The reproducible
host setup is Linux or WSL with Python 3, CMake 3.27 or newer, a POSIX shell,
`patch`, and Zig.

## 1. Build the Win32 ARM translator

Extract `third_party/unicorn-2.1.4.tar.gz` to a working directory, then apply
the included patch from the extracted Unicorn root:

```sh
patch -p1 < /absolute/path/to/source/patches/unicorn-2.1.4-win32-zig.patch
```

Make the helper scripts executable and export their absolute paths:

```sh
export ZIG=/absolute/path/to/zig
export GD_ARM_ZIGCC=/absolute/path/to/source/tools/zigcc-win32.sh
export GD_ARM_ZIGAR=/absolute/path/to/source/tools/zigar.sh
export GD_ARM_ZIGRANLIB=/absolute/path/to/source/tools/zigranlib.sh
export GD_ARM_BUILD_CACHE=/absolute/path/to/build-cache
export PATH=/absolute/path/to/source/tools:$PATH
chmod +x /absolute/path/to/source/tools/*.sh
```

Configure and build only the ARM architecture:

```sh
cmake -S /path/to/unicorn-2.1.4 -B /path/to/unicorn-win32 \
  -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=/absolute/path/to/source/tools/unicorn-win32-zig.cmake \
  -DUNICORN_ARCH=arm \
  -DBUILD_SHARED_LIBS=OFF \
  -DUNICORN_LEGACY_STATIC_ARCHIVE=ON \
  -DUNICORN_BUILD_TESTS=OFF \
  -DUNICORN_INSTALL=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/unicorn-win32 --parallel
```

The expected library is typically `/path/to/unicorn-win32/libunicorn.a`.

## 2. Build the graphical ARM wrapper

```sh
python3 build_arm_wrapper.py \
  --zig "$ZIG" \
  --unicorn-source /path/to/unicorn-2.1.4 \
  --unicorn-lib /path/to/unicorn-win32/libunicorn.a \
  --apk /path/to/Geometry_Dash_1.000.apk \
  --out dist-arm-wrapper
```

`--apk` is optional. When supplied, the builder verifies that the APK contains
a little-endian ELF32/ARM shared library and copies it to the output as
`game.apk`.

The output contains:

- `GeometryDashArmWrapper.exe`
- `RUN_ARM_NATIVE_BOOT.cmd`
- `RUN_ARM_PROBE.cmd`
- `RUN_ARM_RELOCATION_ONLY.cmd`
- `README-ARM-TEST.txt`
- `game.apk` when `--apk` was supplied

## 3. Test modes

Run `RUN_ARM_NATIVE_BOOT.cmd` for the graphical path. It executes the authentic
constructors and `JNI_OnLoad`, creates the Windows OpenGL context, invokes the
registered/exported Cocos `nativeInit`, then drives `nativeRender` from the
Windows message loop.

Run `RUN_ARM_PROBE.cmd` to repeat the successful non-graphical native milestone.
Its expected final line is `RESULT: ARM_NATIVE_PROBE_OK`.

Run `RUN_ARM_RELOCATION_ONLY.cmd` to stop after mapping and relocation.

The graphical branch writes `gd-arm-wrapper.log`. Preserve and send the entire
file after a test, including when a window opens or the game reaches a menu.
Because this is the first graphical bootstrap release, an unimplemented import,
JNI method, or OpenGL call found by a real APK is useful diagnostic output, not
proof that the ARM route failed.

## Legacy probe executable

`build_arm_probe.py` and `src/arm_probe.c` remain available to reproduce the
small standalone `0.9.4-arm-probe1` executable. New development should use
`build_arm_wrapper.py` and `src/arm_wrapper.c`.

Unicorn 2.1.4 is distributed under its own GPL-2.0 license. Its unmodified
upstream source tarball, license, and the exact wrapper patch are included so
the static binary can be rebuilt.
