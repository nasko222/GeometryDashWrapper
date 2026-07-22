# Building the experimental ARM bootstrap probe

The tested host is Linux/WSL with Python 3, CMake 3.27+, a POSIX shell, patch,
and Zig. The generated program is a 32-bit Windows console EXE. Unicorn is
linked statically, so no translator DLL is needed at runtime.

1. Extract `third_party/unicorn-2.1.4.tar.gz` to a working directory.
2. From that directory apply `patches/unicorn-2.1.4-win32-zig.patch` with
   `patch -p1`.
3. Make the four scripts in `tools/` executable and export absolute paths:

```sh
export ZIG=/absolute/path/to/zig
export GD_ARM_ZIGCC=/absolute/path/to/source/tools/zigcc-win32.sh
export GD_ARM_ZIGAR=/absolute/path/to/source/tools/zigar.sh
export GD_ARM_ZIGRANLIB=/absolute/path/to/source/tools/zigranlib.sh
export GD_ARM_BUILD_CACHE=/absolute/path/to/build-cache
export PATH=/absolute/path/to/source/tools:$PATH
```

4. Configure and build only the ARM translator:

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

5. Build the probe EXE:

```sh
python3 build_arm_probe.py \
  --zig "$ZIG" \
  --unicorn-source /path/to/unicorn-2.1.4 \
  --unicorn-lib /path/to/unicorn-win32/libunicorn.a \
  --out dist-arm-probe
```

The expected output is `dist-arm-probe/GeometryDashArmProbe.exe`. Put an early
APK next to it as `game.apk`, then run the command file supplied in the binary
test package. The current success milestone is
`RESULT: ARM_NATIVE_PROBE_OK`; there is deliberately no graphical boot yet.

Unicorn 2.1.4 is distributed under its own GPL-2.0 license. Its unmodified
upstream source tarball and the exact wrapper patch are included so the static
binary can be rebuilt.
