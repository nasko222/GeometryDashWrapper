#!/usr/bin/env python3
"""Build the 32-bit Windows wrapper for x86 Android Geometry Dash APKs."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
import zlib
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
    members = ("lib/x86/libcocos2dcpp.so", "lib/x86/libgame.so")
    with zipfile.ZipFile(apk) as archive:
        member = next((name for name in members if name in archive.namelist()), None)
        if member is None:
            raise RuntimeError(
                "APK does not contain lib/x86/libcocos2dcpp.so or "
                "lib/x86/libgame.so"
            )
        with archive.open(member) as stream:
            header = stream.read(20)
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


def generate_embedded_effects(
    apks: list[Path], destination: Path, ffmpeg: Path | None
) -> None:
    effects: list[tuple[str, bytes, int]] = []
    if not apks:
        destination.write_text(
            '#include "embedded_effects.h"\n'
            'const EmbeddedEffect *embedded_effect_find(const char *name) '
            '{ (void)name; return 0; }\n',
            encoding="utf-8",
        )
        return
    assert ffmpeg is not None
    payloads: dict[str, bytes] = {}
    for apk in apks:
        with zipfile.ZipFile(apk) as archive:
            members = sorted(
                name for name in archive.namelist()
                if name.startswith("assets/") and name.lower().endswith(".ogg")
            )
            for member in members:
                name = Path(member).name
                payload = archive.read(member)
                previous = payloads.get(name)
                if previous is not None and previous != payload:
                    raise RuntimeError(
                        f"Conflicting Ogg effect {name!r} in {apk}; a universal "
                        "build needs version-aware effect selection"
                    )
                payloads[name] = payload

    with tempfile.TemporaryDirectory() as temporary:
        temporary_path = Path(temporary)
        for index, (name, payload) in enumerate(sorted(payloads.items())):
            source = temporary_path / f"source-{index}.ogg"
            target = temporary_path / f"effect-{index}.wav"
            source.write_bytes(payload)
            subprocess.run(
                [
                    str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
                    "-i", str(source), "-vn", "-ar", "44100", "-ac", "1",
                    "-c:a", "pcm_s16le", str(target),
                ],
                check=True,
            )
            wav = target.read_bytes()
            effects.append(
                (Path(name).with_suffix(".wav").name, zlib.compress(wav, 9), len(wav))
            )

    if not effects:
        destination.write_text(
            '#include "embedded_effects.h"\n'
            'const EmbeddedEffect *embedded_effect_find(const char *name) '
            '{ (void)name; return 0; }\n',
            encoding="utf-8",
        )
        return

    lines = ['#include <string.h>', '#include "embedded_effects.h"', '']
    for index, (_, compressed, _) in enumerate(effects):
        lines.append(f"static const unsigned char effect_{index}[] = {{")
        for offset in range(0, len(compressed), 16):
            chunk = compressed[offset : offset + 16]
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
        lines.append("};")
    lines.extend(["", "static const EmbeddedEffect effects[] = {"])
    for index, (name, compressed, uncompressed_size) in enumerate(effects):
        lines.append(
            f"    {{{json.dumps(name)}, effect_{index}, {len(compressed)}u, "
            f"{uncompressed_size}u}},"
        )
    lines.extend(
        [
            "};",
            "",
            "const EmbeddedEffect *embedded_effect_find(const char *name) {",
            "    size_t index;",
            "    if (!name) return 0;",
            "    for (index = 0; index < sizeof(effects) / sizeof(effects[0]); ++index) {",
            "        if (strcmp(name, effects[index].name) == 0) return &effects[index];",
            "    }",
            "    return 0;",
            "}",
            "",
        ]
    )
    destination.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", help="Path to Zig")
    parser.add_argument("--ffmpeg", help="Path to FFmpeg for Ogg effect conversion")
    parser.add_argument("--apk", type=Path, help="Optional Geometry Dash APK with x86 code")
    parser.add_argument(
        "--effects-apk", type=Path, action="append", default=[],
        help="Additional APK whose Ogg effects should be embedded in the same EXE; repeatable",
    )
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    zig = locate_zig(args.zig)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    apk = args.apk.resolve() if args.apk else None
    effect_apks = [path.resolve() for path in args.effects_apk]
    ffmpeg = None
    if apk:
        validate_apk(apk)
        effect_apks.insert(0, apk)
    for effect_apk in effect_apks:
        if not effect_apk.is_file():
            raise RuntimeError(f"Effects APK was not found: {effect_apk}")
    if effect_apks:
        ffmpeg = locate_ffmpeg(args.ffmpeg)
    cache = root / "build-cache"
    cache.mkdir(exist_ok=True)
    generated = cache / "generated"
    generated.mkdir(exist_ok=True)
    embedded_effects = generated / "embedded_effects.c"
    generate_embedded_effects(effect_apks, embedded_effects, ffmpeg)

    sources = [
        root / "src/main.c",
        root / "src/loader.c",
        root / "src/runtime.c",
        root / "src/bionic_x86.S",
        root / "src/jni_shim.c",
        root / "src/audio_win.c",
        root / "src/fmod_win.c",
        root / "src/storage_win.c",
        embedded_effects,
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
        f"-I{root / 'src'}",
        "-o",
        str(output / "GeometryDashWrapper.exe"),
        *(str(path) for path in sources),
        "-lws2_32",
        "-lopengl32",
        "-lgdi32",
        "-luser32",
        "-lshell32",
        "-lwinmm",
        "-lole32",
    ]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(cache / "global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(cache / "local")
    subprocess.run(command, check=True, env=environment)

    (output / "GeometryDash18Wrapper.exe").unlink(missing_ok=True)
    (output / "GeometryDash18Wrapper.pdb").unlink(missing_ok=True)
    (output / "libcocos2dcpp.so").unlink(missing_ok=True)
    (output / "libgame.so").unlink(missing_ok=True)
    shutil.rmtree(output / "audio", ignore_errors=True)

    if apk:
        shutil.copy2(apk, output / "game.apk")

    print(output / "GeometryDashWrapper.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
