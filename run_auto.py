#!/usr/bin/env python3
from pathlib import Path
import os
import struct
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parent
base = root / "dist-unified" if (root / "dist-unified").is_dir() else root
apk = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else base / "game.apk"
if not apk.is_file():
    raise SystemExit(f"APK not found: {apk}")


def setting_bool(name: str, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None or not value.strip():
        return default
    normalized = value.strip().lower()
    if normalized in {"true", "yes", "on", "1"}:
        return True
    if normalized in {"false", "no", "off", "0"}:
        return False
    return default


def png_size(payload: bytes) -> tuple[int, int] | None:
    if len(payload) < 24 or payload[:8] != b"\x89PNG\r\n\x1a\n" or payload[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", payload[16:24])
    if width < 1 or height < 1 or width > 4096 or height > 4096:
        return None
    return width, height


def write_png_ico(payload: bytes, destination: Path) -> bool:
    size = png_size(payload)
    if not size:
        return False
    width, height = size
    entry_width = 0 if width >= 256 else width
    entry_height = 0 if height >= 256 else height
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack(
        "<BBBBHHII", entry_width, entry_height, 0, 0, 1, 32,
        len(payload), 6 + 16,
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + entry + payload)
    return True


def choose_apk_icon(archive: zipfile.ZipFile) -> tuple[str, bytes] | None:
    density_score = {
        "xxxhdpi": 600, "xxhdpi": 500, "xhdpi": 400,
        "hdpi": 300, "mdpi": 200, "ldpi": 100,
    }
    rejected = (
        "notification", "notify", "ad_", "ads", "google", "facebook",
        "twitter", "amazon", "exo_", "mbridge", "privacy", "signin",
        "play_", "billing", "achievement",
    )
    candidates: list[tuple[int, str, bytes]] = []
    for name in archive.namelist():
        lower = name.lower()
        if not lower.endswith(".png") or not lower.startswith("res/"):
            continue
        basename = Path(lower).name
        if any(word in basename for word in rejected):
            continue
        is_icon_name = "icon" in basename or "launcher" in basename
        is_mipmap = "/mipmap" in lower
        if not is_icon_name and not is_mipmap:
            continue
        try:
            payload = archive.read(name)
        except (KeyError, RuntimeError):
            continue
        size = png_size(payload)
        if not size:
            continue
        width, height = size
        score = min(width, height) * 10
        if is_icon_name:
            score += 5000
        if basename == "icon.png":
            score += 15000
        if is_mipmap:
            score += 2500
        if basename.startswith("ic_launcher"):
            score += 18000
        if basename in {"ic_f.png", "ic_r.png"}:
            score += 3000
        for key, value in density_score.items():
            if key in lower:
                score += value
                break
        if abs(width - height) <= max(width, height) // 8:
            score += 500
        candidates.append((score, name, payload))
    if not candidates:
        return None
    _, name, payload = max(candidates, key=lambda item: (item[0], len(item[2])))
    return name, payload


with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    icon_source = ""
    icon_payload = None
    for override in (base / "icon.png", root / "icon.png"):
        if override.is_file():
            candidate = override.read_bytes()
            if png_size(candidate):
                icon_source = str(override)
                icon_payload = candidate
                break
    if icon_payload is None:
        selected_icon = choose_apk_icon(archive)
        if selected_icon:
            icon_source, icon_payload = selected_icon

save = base / "save"
save.mkdir(parents=True, exist_ok=True)
if icon_payload is not None:
    icon_path = save / "wrapper-icon.ico"
    if write_png_ico(icon_payload, icon_path):
        os.environ["GD_WINDOW_ICON"] = str(icon_path.resolve())
        print(f"Window icon: {icon_source}")

has_x86 = "lib/x86/libcocos2dcpp.so" in names or "lib/x86/libgame.so" in names
has_legacy_arm = "lib/armeabi/libgame.so" in names
has_armv7 = "lib/armeabi-v7a/libcocos2dcpp.so" in names
override_arm = setting_bool("OVERRIDE_ARM", False)
if override_arm and (has_armv7 or has_legacy_arm):
    if has_armv7:
        backend = "armv7"
        exe = base / "armv7" / "GeometryDashArmV7.exe"
        args = [str(exe), str(apk), "--companion-hooks=off", "--log=gd-armv7.log"]
    else:
        backend = "arm-legacy"
        exe = base / "arm-legacy" / "GeometryDashArmLegacy.exe"
        args = [str(exe), str(apk), "--log=gd-arm-legacy.log"]
elif has_x86:
    backend = "x86"
    exe = base / "x86" / "GeometryDashWrapper.exe"
    args = [str(exe), f"--apk={apk}"]
elif has_legacy_arm:
    backend = "arm-legacy"
    exe = base / "arm-legacy" / "GeometryDashArmLegacy.exe"
    args = [str(exe), str(apk), "--log=gd-arm-legacy.log"]
elif has_armv7:
    backend = "armv7"
    exe = base / "armv7" / "GeometryDashArmV7.exe"
    args = [str(exe), str(apk), "--companion-hooks=off", "--log=gd-armv7.log"]
else:
    discovered = sorted(name for name in names if name.startswith("lib/") and name.endswith(".so"))
    details = ", ".join(discovered) if discovered else "no native .so files"
    raise SystemExit(
        "APK has no supported game library. Expected one of: "
        "lib/x86/libcocos2dcpp.so, lib/x86/libgame.so, "
        "lib/armeabi/libgame.so, or lib/armeabi-v7a/libcocos2dcpp.so. "
        f"Found: {details}"
    )

if not exe.is_file():
    raise SystemExit(f"Selected {backend}, but the backend is not built: {exe}")

print(f"Selected backend: {backend}")
print(f"Shared save folder: {save}")
print(f"GDPS server: {os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}")
print(f"Hack icons and colors: {setting_bool('HACK_ICONS', False)}")
print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
print(f"Force highest graphics: {setting_bool('FORCE_HIGHEST_GRAPHICS', True)}")
print(f"Music pulse max: {os.environ.get('MUSIC_PULSE_MAX', '0.30')}")
print(f"Override ARM: {override_arm}")
raise SystemExit(subprocess.call(args, cwd=base, env=os.environ.copy()))
