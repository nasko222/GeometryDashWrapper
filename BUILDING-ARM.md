# Building GD Wrapper 0.9.4-arm-bootstrap15

## Windows: no Linux, WSL, admin rights, or installer

Use the full-source archive. Keep `game.apk` in the source folder, or drag an
APK onto `BUILD_WINDOWS.cmd` / pass its path on the command line:

```text
BUILD_WINDOWS.cmd C:\Games\GeometryDash-1.4.apk
```

The script downloads verified portable copies of Zig 0.14.1, CMake 3.31.10,
and Ninja 1.13.2 into `.build-tools/`. It then builds the included patched
Unicorn 2.1.4 source as an ARM-only static Win32 library and compiles the
32-bit wrapper. The Windows route uses bundled Win32 QEMU configuration headers,
so it does not require Bash, MSYS2, Git Bash, WSL, or a Linux VM. Nothing is
installed in Program Files, the registry, or the system PATH.

The result is written to:

```text
dist-arm-wrapper-bootstrap15\
```

It contains `GeometryDashArmWrapper.exe`, launchers, build information, and
`game.apk` when one was supplied. The downloaded tools and build cache can be
deleted at any time; later builds will simply download/rebuild them again.

Useful commands:

```text
BUILD_WINDOWS.cmd
BUILD_WINDOWS.cmd C:\path\to\game.apk
BUILD_WINDOWS.cmd -Clean
```

The first command uses `game.apk` from the source folder. `-Clean` removes the
compiled caches before rebuilding. Internet access is needed only when the
portable tools are not already cached.

## Test modes

- `RUN_ARM_NATIVE_BOOT.cmd` starts the graphical wrapper.
- `RUN_ARM_PROBE.cmd` stops after constructors and `JNI_OnLoad`.
- `RUN_ARM_RELOCATION_ONLY.cmd` stops after mapping and relocation.
- Add `--deep-diagnostics` manually only for corruption/parser investigation;
  it enables intentionally expensive instruction-level hooks.

Preserve the entire `gd-arm-wrapper.log` after testing menu startup, repeated
deaths, Clutterfunk, Xstep, Cycles, and editor load/save.

## Manual Linux/WSL route

The older reproducible route remains supported for developers who already have
Python 3, CMake 3.27+, a POSIX shell, `patch`, and Zig.

Extract `third_party/unicorn-2.1.4.tar.gz`, apply
`patches/unicorn-2.1.4-win32-zig.patch`, export the helper scripts in `tools/`,
and configure an ARM-only static build:

```sh
patch -p1 < /absolute/path/to/source/patches/unicorn-2.1.4-win32-zig.patch
export ZIG=/absolute/path/to/zig
export GD_ARM_ZIGCC=/absolute/path/to/source/tools/zigcc-win32.sh
export GD_ARM_ZIGAR=/absolute/path/to/source/tools/zigar.sh
export GD_ARM_ZIGRANLIB=/absolute/path/to/source/tools/zigranlib.sh
export GD_ARM_BUILD_CACHE=/absolute/path/to/build-cache
export PATH=/absolute/path/to/source/tools:$PATH
chmod +x /absolute/path/to/source/tools/*.sh

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

python3 build_arm_wrapper.py \
  --zig "$ZIG" \
  --unicorn-source /path/to/unicorn-2.1.4 \
  --unicorn-lib /path/to/unicorn-win32/libunicorn.a \
  --apk /path/to/Geometry_Dash_1.000.apk \
  --out dist-arm-wrapper
```

Unicorn 2.1.4 is distributed under its own GPL-2.0 license. Its upstream source,
license, and exact wrapper patch are included in the full-source archive.

Builder6 does not create Unicorn's legacy `unicorn.o` symbolic link on Windows. No Developer Mode or administrator privileges are required; the wrapper links directly against `libunicorn.a`.
