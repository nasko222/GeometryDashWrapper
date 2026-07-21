#!/usr/bin/env python3
"""Build the 32-bit Windows Geometry Dash 1.8 native-wrapper probe."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import zipfile
from pathlib import Path


EXPECTED_SO_SHA256 = "829bea8061e4136c584633a301c438ac2608641108a5a10fcf31672075dfbb4d"


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def locate_zig(value: str | None) -> Path:
    if value:
        candidate = Path(value)
    elif os.environ.get("ZIG"):
        candidate = Path(os.environ["ZIG"])
    else:
        found = shutil.which("zig")
        if not found:
            raise RuntimeError("Zig was not found. Pass --zig or set the ZIG environment variable.")
        candidate = Path(found)
    if not candidate.is_file():
        raise RuntimeError(f"Zig executable not found: {candidate}")
    return candidate.resolve()


def extract_library(apk: Path, destination: Path) -> None:
    with zipfile.ZipFile(apk) as archive:
        member = "lib/x86/libcocos2dcpp.so"
        try:
            payload = archive.read(member)
        except KeyError as error:
            raise RuntimeError(f"APK does not contain {member}") from error
    destination.write_bytes(payload)
    actual = file_hash(destination)
    if actual != EXPECTED_SO_SHA256:
        destination.unlink(missing_ok=True)
        raise RuntimeError(
            "This is not the expected intact Geometry Dash 1.8 x86 library: " + actual
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", help="Path to Zig")
    parser.add_argument("--apk", type=Path, help="Optional original 1.8 APK")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    zig = locate_zig(args.zig)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = root / "build-cache"
    cache.mkdir(exist_ok=True)

    sources = [
        root / "src/main.c",
        root / "src/loader.c",
        root / "src/runtime.c",
        *sorted((root / "third_party/zlib").glob("*.c")),
    ]
    command = [
        str(zig),
        "cc",
        "-target",
        "x86-windows-gnu",
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wno-cast-function-type",
        "-Wno-deprecated-non-prototype",
        "-mstackrealign",
        f"-I{root / 'third_party/zlib'}",
        "-o",
        str(output / "GeometryDash18Wrapper.exe"),
        *(str(path) for path in sources),
        "-lws2_32",
    ]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(cache / "global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(cache / "local")
    subprocess.run(command, check=True, env=environment)

    if args.apk:
        extract_library(args.apk.resolve(), output / "libcocos2dcpp.so")

    print(output / "GeometryDash18Wrapper.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
