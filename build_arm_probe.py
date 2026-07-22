#!/usr/bin/env python3
"""Build the 32-bit Windows ARM bootstrap probe with a static Unicorn library."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def executable(value: str | None, name: str) -> Path:
    candidate = Path(value) if value else Path(shutil.which(name) or "")
    if not candidate.is_file():
        raise RuntimeError(f"{name} was not found; pass --{name}")
    return candidate.resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", help="Path to the Zig executable")
    parser.add_argument(
        "--unicorn-source", type=Path, required=True,
        help="Extracted, patched Unicorn 2.1.4 source directory",
    )
    parser.add_argument(
        "--unicorn-lib", type=Path, required=True,
        help="Win32 libunicorn.a produced from the patched source",
    )
    parser.add_argument("--out", type=Path, default=Path("dist-arm-probe"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    zig = executable(args.zig, "zig")
    unicorn_source = args.unicorn_source.resolve()
    unicorn_lib = args.unicorn_lib.resolve()
    if not (unicorn_source / "include/unicorn/unicorn.h").is_file():
        raise RuntimeError("--unicorn-source does not contain Unicorn headers")
    if not unicorn_lib.is_file():
        raise RuntimeError(f"Unicorn static library was not found: {unicorn_lib}")

    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = root / "build-cache-arm"
    cache.mkdir(exist_ok=True)
    sources = [
        root / "src/arm_probe.c",
        *sorted((root / "third_party/zlib").glob("*.c")),
    ]
    command = [
        str(zig), "cc", "-target", "x86-windows-gnu", "-std=c11", "-O2",
        "-Wall", "-Wextra", "-Wno-cast-function-type",
        "-Dcrc32=gd_z_crc32",
        f"-I{unicorn_source / 'include'}",
        f"-I{root / 'src'}",
        f"-I{root / 'third_party/zlib'}",
        *(str(source) for source in sources),
        str(unicorn_lib),
        "-o", str(output / "GeometryDashArmProbe.exe"),
        "-lwinmm", "-lws2_32",
    ]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(cache / "global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(cache / "local")
    subprocess.run(command, check=True, env=environment)
    print(output / "GeometryDashArmProbe.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
