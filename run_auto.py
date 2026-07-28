#!/usr/bin/env python3
from __future__ import annotations

import argparse
import configparser
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
BASE = ROOT / "dist-unified" if (ROOT / "dist-unified").is_dir() else ROOT


def read_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    parser = configparser.ConfigParser(interpolation=None)
    if path.is_file():
        parser.read(path, encoding="utf-8")
        for key, value in parser.defaults().items():
            values[key.lower()] = value.strip()
        for section in parser.sections():
            for key, value in parser.items(section):
                values[key.lower()] = value.strip()
    return values


def config_bool(values: dict[str, str], name: str, default: bool) -> bool:
    raw = values.get(name.lower())
    if raw is None or not raw.strip():
        return default
    normalized = raw.strip().lower()
    if normalized in {"true", "yes", "on", "1"}:
        return True
    if normalized in {"false", "no", "off", "0"}:
        return False
    return default


def setting_bool(name: str, default: bool = False) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    normalized = raw.strip().lower()
    if normalized in {"true", "yes", "on", "1"}:
        return True
    if normalized in {"false", "no", "off", "0"}:
        return False
    return default


def manifest_package(payload: bytes) -> str:
    texts = [
        payload.decode("utf-16le", errors="ignore"),
        payload.decode("utf-8", errors="ignore"),
    ]
    matches: list[str] = []
    for text in texts:
        matches.extend(re.findall(r"com\.[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+", text))
    preferred = [
        value for value in matches
        if "robtop" in value.lower() or "gdps" in value.lower()
    ]
    values = preferred or matches
    if not values:
        return "unknown.package"
    return min(values, key=lambda value: (value.count("."), len(value)))


def game_kind_for(package: str, names: set[str]) -> str:
    value = package.lower()
    lowered_names = {name.lower() for name in names}
    if "subzero" in value or any("leveldatasubzero" in name for name in lowered_names):
        return "subzero"
    if "meltdown" in value and "world" not in value:
        return "meltdown"
    # Lite must be checked before generic World asset heuristics. Some Lite APKs
    # bundle shared World data even though their package and UI are Lite.
    if "geometryjumplite" in value or value.endswith(".lite") or "dashlite" in value:
        return "lite"
    if "world" in value or any("leveldataworld" in name for name in lowered_names):
        return "world"
    return "geometry-dash"


def game_title_for(kind: str) -> str:
    return {
        "lite": "Geometry Dash Lite",
        "world": "Geometry Dash World",
        "meltdown": "Geometry Dash Meltdown",
        "subzero": "Geometry Dash SubZero",
    }.get(kind, "Geometry Dash")


def resolve_relative(value: str, base: Path) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def apply_settings(values: dict[str, str]) -> None:
    mapping = {
        "gdps_server": "GDPS_SERVER",
        "hack_icons": "HACK_ICONS",
        "full_bypass": "FULL_BYPASS",
        "force_highest_graphics": "FORCE_HIGHEST_GRAPHICS",
        "music_pulse_max": "MUSIC_PULSE_MAX",
    }
    defaults = {
        "GDPS_SERVER": "www.boomlings.com/database",
        "HACK_ICONS": "false",
        "FULL_BYPASS": "true",
        "FORCE_HIGHEST_GRAPHICS": "true",
        "MUSIC_PULSE_MAX": "0.30",
    }
    for env_name, default in defaults.items():
        os.environ.setdefault(env_name, default)
    for key, env_name in mapping.items():
        value = values.get(key)
        if value is not None and value.strip():
            os.environ[env_name] = value.strip()


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("apk", nargs="?", help="APK override")
    parser.add_argument("--cfg", default="GeometryDash.cfg", help="Launcher config")
    args = parser.parse_args()

    cfg_path = resolve_relative(args.cfg, BASE)
    settings = read_config(cfg_path)
    apply_settings(settings)

    apk_setting = args.apk or settings.get("apk", "game.apk")
    apk = resolve_relative(apk_setting, BASE)
    if not apk.is_file():
        raise SystemExit(f"APK not found: {apk}")

    with zipfile.ZipFile(apk) as archive:
        names = set(archive.namelist())
        try:
            package = manifest_package(archive.read("AndroidManifest.xml"))
        except KeyError:
            package = "unknown.package"

    kind = game_kind_for(package, names)
    title = game_title_for(kind)
    save_root = BASE / "save"
    save_root.mkdir(parents=True, exist_ok=True)
    os.environ["GD_SAVE_DIR"] = str(save_root.resolve())
    os.environ["GD_GAME_TITLE"] = title
    os.environ["GD_GAME_KIND"] = kind

    icon = BASE / "assets" / "icons" / f"{kind}.ico"
    if not icon.is_file():
        icon = BASE / "assets" / "icons" / "geometry-dash.ico"
    if icon.is_file():
        os.environ["GD_WINDOW_ICON"] = str(icon.resolve())

    has_x86 = (
        "lib/x86/libcocos2dcpp.so" in names or
        "lib/x86/libgame.so" in names
    )
    has_legacy_arm = "lib/armeabi/libgame.so" in names
    has_armv7 = "lib/armeabi-v7a/libcocos2dcpp.so" in names
    debug = config_bool(settings, "debug", False)

    if has_x86:
        backend = "x86"
        module = BASE / "backends" / "x86" / "GeometryDashX86.dll"
        command = [str(module), f"--apk={apk}"]
        if debug:
            command += ["--debug-everything"]
    elif has_legacy_arm:
        backend = "arm-legacy"
        module = BASE / "backends" / "arm-legacy" / "GeometryDashArmLegacy.dll"
        command = [str(module), str(apk), "--log=gd-arm-legacy.log"]
        if debug:
            command += [
                "--debug-everything",
                "--dump-imports=gd-arm-legacy-imports.txt",
                "--profile=gd-arm-legacy-profile.csv",
                "--profile-summary=gd-arm-legacy-profile-summary.txt",
            ]
    elif has_armv7:
        backend = "armv7"
        module = BASE / "backends" / "armv7" / "GeometryDashArmV7.dll"
        command = [
            str(module), str(apk), "--companion-hooks=off", "--log=gd-armv7.log"
        ]
        if debug:
            command += [
                "--debug-everything",
                "--dump-imports=gd-armv7-imports.txt",
                "--profile=gd-dynarmic-profile.csv",
                "--profile-summary=gd-dynarmic-profile-summary.txt",
            ]
    else:
        discovered = sorted(
            name for name in names
            if name.startswith("lib/") and name.endswith(".so")
        )
        details = ", ".join(discovered) if discovered else "no native .so files"
        raise SystemExit(
            "APK has no supported game library. Expected x86 libgame/"
            "libcocos2dcpp, armeabi libgame, or armeabi-v7a libcocos2dcpp. "
            f"Found: {details}"
        )

    if not module.is_file():
        raise SystemExit(f"Selected {backend}, but the backend module is not built: {module}")

    print(f"Game: {title}")
    print(f"Package: {package}")
    print(f"Selected backend: {backend}")
    print(f"Save directory: {save_root}")
    print(f"Config: {cfg_path}")
    print(f"GDPS server: {os.environ['GDPS_SERVER']}")
    print(f"Hack icons and colors: {setting_bool('HACK_ICONS', False)}")
    print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
    print(f"Force highest graphics: {setting_bool('FORCE_HIGHEST_GRAPHICS', True)}")
    print(f"Music pulse max: {os.environ['MUSIC_PULSE_MAX']}")

    # The backend files retain executable PE headers but use private .dll names
    # so one x64 launcher can start either x86 or x64 backends out-of-process.
    creationflags = 0
    if os.name == "nt" and config_bool(settings, "disable_windows_console", True):
        creationflags = subprocess.CREATE_NO_WINDOW
    return subprocess.call(
        command,
        cwd=BASE,
        env=os.environ.copy(),
        executable=str(module),
        creationflags=creationflags,
    )


if __name__ == "__main__":
    raise SystemExit(main())
