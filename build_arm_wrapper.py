#!/usr/bin/env python3
"""Build the experimental 32-bit Windows ARM graphical wrapper."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

ARM_MEMBERS = (
    "lib/armeabi/libgame.so",
    "lib/armeabi/libcocos2dcpp.so",
    "lib/armeabi-v7a/libgame.so",
    "lib/armeabi-v7a/libcocos2dcpp.so",
)


def executable(value: str | None, name: str) -> Path:
    candidate = Path(value) if value else Path(shutil.which(name) or "")
    if not candidate.is_file():
        raise RuntimeError(f"{name} was not found; pass --{name}")
    return candidate.resolve()


def validate_arm_apk(apk: Path) -> str:
    if not apk.is_file():
        raise RuntimeError(f"APK was not found: {apk}")
    with zipfile.ZipFile(apk) as archive:
        member = next((name for name in ARM_MEMBERS if name in archive.namelist()), None)
        if not member:
            raise RuntimeError("APK does not contain a supported ARM game library")
        header = archive.read(member)[:20]
    if (
        len(header) < 20
        or header[:6] != b"\x7fELF\x01\x01"
        or int.from_bytes(header[16:18], "little") != 3
        or int.from_bytes(header[18:20], "little") != 40
    ):
        raise RuntimeError(f"{member} is not a little-endian ELF32/ARM shared object")
    return member


def write_launchers(output: Path) -> None:
    launchers = {
        "RUN_ARM_NATIVE_BOOT.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo OVERKILL visual mode: audio and cosmetics removed; textures become 1x1 white.\r\n"
            "echo Hotkeys: F3 nativeRender, F4 ARM draws, F5 particles, F6 host draws, F7 scene, F8 GL, F9 textures, F10 state.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --overkill\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_CONTROL_PERFORMANCETEST1.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Control run: all normal performancetest1 subsystems enabled.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_BARE_MINIMUM.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Bare minimum: audio, particles, ARM draw methods, textures and host OpenGL removed.\r\n"
            "echo Game update/collision and scene traversal still execute. Window will be blank.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --headless-gl --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_ZERO_RENDER.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Zero-render baseline: nativeRender is never called. Window will be blank.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --skip-native-render --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_NO_ARM_DRAWS.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo ARM sprite, batch, atlas, layer, label and shape draw methods removed.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_NO_TEXTURES.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Audio, cosmetics and all texture uploads removed.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-textures\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_NO_DRAWS.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Diagnostic: ARM still builds the scene, but every host draw call is discarded.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-textures --skip-draws --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_LOGIC_ONLY.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Diagnostic: CCNode scene traversal is removed. Window may be blank.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --no-node-draws --no-textures --skip-scene-visit --headless-gl --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_OVERKILL_HEADLESS_GL.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Diagnostic: all OpenGL calls use a fake headless backend. Window will be blank.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --no-audio --no-particles --headless-gl --no-vsync --uncapped\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_PROFILE_IMPORT_TIME.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Diagnostic callback-timing run; test one heavy section.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --overkill --profile-import-time\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_PROFILE_ARM_BLOCKS.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Diagnostic ARM block profiler; test one heavy section.\r\n"
            "GeometryDashArmWrapper.exe --apk=game.apk --overkill --profile-arm-blocks\r\n"
            "if errorlevel 1 pause\r\n"
        ),
        "RUN_ARM_PROBE.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "GeometryDashArmWrapper.exe --probe --apk=game.apk --no-audio\r\n"
            "pause\r\n"
        ),
        "RUN_ARM_RELOCATION_ONLY.cmd": (
            "@echo off\r\n"
            "cd /d \"%~dp0\"\r\n"
            "GeometryDashArmWrapper.exe --relocate-only --apk=game.apk --no-audio\r\n"
            "pause\r\n"
        ),
    }
    for name, content in launchers.items():
        (output / name).write_text(content, encoding="utf-8")


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
    parser.add_argument("--apk", type=Path, help="Optional ARM APK copied as game.apk")
    parser.add_argument("--out", type=Path, default=Path("dist-arm-wrapper"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    zig = executable(args.zig, "zig")
    unicorn_source = args.unicorn_source.resolve()
    unicorn_lib = args.unicorn_lib.resolve()
    if not (unicorn_source / "include/unicorn/unicorn.h").is_file():
        raise RuntimeError("--unicorn-source does not contain Unicorn headers")
    if not unicorn_lib.is_file():
        raise RuntimeError(f"Unicorn static library was not found: {unicorn_lib}")

    apk = args.apk.resolve() if args.apk else None
    if apk:
        member = validate_arm_apk(apk)
        print(f"Validated {member}")

    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = root / "build-cache-arm-wrapper"
    generated = cache / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    embedded_effects = generated / "embedded_effects.c"
    embedded_effects.write_text(
        '#include "embedded_effects.h"\n'
        'const EmbeddedEffect *embedded_effect_find(const char *name) '
        '{ (void)name; return 0; }\n',
        encoding="utf-8",
    )

    sources = [
        root / "src/arm_wrapper.c",
        root / "src/audio_win.c",
        root / "src/storage_win.c",
        embedded_effects,
        root / "third_party/stb/stb_vorbis.c",
        *sorted((root / "third_party/zlib").glob("*.c")),
    ]
    command = [
        str(zig), "cc", "-target", "x86-windows-gnu", "-std=c11", "-O3",
        "-Wall", "-Wextra", "-Wno-cast-function-type",
        "-Wno-deprecated-non-prototype", "-mstackrealign",
        "-Dcrc32=gd_z_crc32",
        f"-I{unicorn_source / 'include'}",
        f"-I{root / 'src'}",
        f"-I{root / 'third_party/zlib'}",
        *(str(source) for source in sources),
        str(unicorn_lib),
        "-o", str(output / "GeometryDashArmWrapper.exe"),
        "-lws2_32", "-lopengl32", "-lgdi32", "-luser32",
        "-lshell32", "-lwinmm", "-lole32",
    ]
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(cache / "global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(cache / "local")
    subprocess.run(command, check=True, env=environment)

    if apk:
        shutil.copy2(apk, output / "game.apk")
    write_launchers(output)
    (output / "README-ARM-TEST.txt").write_text(
        "Geometry Dash ARM Wrapper 0.9.4-arm-overkilltest1\n\n"
        "Place a supported ARM APK beside the EXE as game.apk, then run "
        "RUN_ARM_NATIVE_BOOT.cmd. OverkillTest1 keeps performancetest1's integrity protections but adds destructive "
        "diagnostic modes. Audio can be removed before initialization; particle, trail "
        "and cosmetic guest functions are hot-patched to return; original PNG assets "
        "can be replaced by a 70-byte 1x1 image; ARM sprite/label/shape draw methods, "
        "host draw calls, CCNode scene traversal, nativeRender itself, and the complete "
        "host OpenGL backend can each be disabled independently. Use the included "
        "launchers and F3-F10 hotkeys. "
        "Deep parser hooks are disabled by "
        "default; use --deep-diagnostics only for corruption investigation. "
        "Send the complete gd-arm-wrapper.log after the same heavy Clutterfunk section in each diagnostic mode.\n",
        encoding="utf-8",
    )
    print(output / "GeometryDashArmWrapper.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
