#!/usr/bin/env python3
"""Build the 32-bit Windows wrapper for x86 Android Geometry Dash APKs."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


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


def validate_apk(apk: Path) -> None:
    with zipfile.ZipFile(apk) as archive:
        member = "lib/x86/libcocos2dcpp.so"
        try:
            with archive.open(member) as stream:
                header = stream.read(20)
        except KeyError as error:
            raise RuntimeError(f"APK does not contain the required {member}") from error
    if (
        len(header) < 20
        or header[:6] != b"\x7fELF\x01\x01"
        or int.from_bytes(header[16:18], "little") != 3
        or int.from_bytes(header[18:20], "little") != 3
    ):
        raise RuntimeError(f"{member} is not a little-endian ELF32/i386 shared object")


def locate_ffmpeg(value: str | None) -> Path:
    candidate = value or os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not candidate or not Path(candidate).is_file():
        raise RuntimeError(
            "FFmpeg is required to convert the APK's six Ogg effects to "
            "Windows PCM WAV files. Pass --ffmpeg or set FFMPEG."
        )
    return Path(candidate).resolve()


def extract_audio(apk: Path, destination: Path, ffmpeg: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(apk) as archive, tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        members = sorted(
            name for name in archive.namelist()
            if name.startswith("assets/") and name.lower().endswith((".mp3", ".ogg"))
        )
        for member in members:
            name = Path(member).name
            payload = archive.read(member)
            if name.lower().endswith(".mp3"):
                (destination / name).write_bytes(payload)
                continue
            source = temporary_path / name
            target = destination / (Path(name).stem + ".wav")
            source.write_bytes(payload)
            subprocess.run(
                [
                    str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
                    "-i", str(source), "-vn", "-c:a", "pcm_s16le", str(target),
                ],
                check=True,
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", help="Path to Zig")
    parser.add_argument("--ffmpeg", help="Path to FFmpeg for Ogg effect conversion")
    parser.add_argument("--apk", type=Path, help="Optional Geometry Dash APK with x86 code")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    zig = locate_zig(args.zig)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.apk:
        validate_apk(args.apk.resolve())
    cache = root / "build-cache"
    cache.mkdir(exist_ok=True)

    sources = [
        root / "src/main.c",
        root / "src/loader.c",
        root / "src/runtime.c",
        root / "src/jni_shim.c",
        root / "src/audio_win.c",
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
        str(output / "GeometryDashWrapper.exe"),
        *(str(path) for path in sources),
        "-lws2_32",
        "-lopengl32",
        "-lgdi32",
        "-luser32",
        "-lwinmm",
    ]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(cache / "global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(cache / "local")
    subprocess.run(command, check=True, env=environment)

    (output / "GeometryDash18Wrapper.exe").unlink(missing_ok=True)
    (output / "GeometryDash18Wrapper.pdb").unlink(missing_ok=True)
    (output / "libcocos2dcpp.so").unlink(missing_ok=True)

    if args.apk:
        apk = args.apk.resolve()
        shutil.copy2(apk, output / "game.apk")
        extract_audio(apk, output / "audio", locate_ffmpeg(args.ffmpeg))

    print(output / "GeometryDashWrapper.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
