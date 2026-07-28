#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parent
base = root / "dist-unified" if (root / "dist-unified").is_dir() else root
apk = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else base / "game.apk"
if not apk.is_file():
    raise SystemExit(f"APK not found: {apk}")

with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())

# Explicit priority order: native x86 first unless OVERRIDE_ARM=true.
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

has_x86 = (
    "lib/x86/libcocos2dcpp.so" in names or "lib/x86/libgame.so" in names
)
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
    discovered = sorted(
        name for name in names if name.startswith("lib/") and name.endswith(".so")
    )
    details = ", ".join(discovered) if discovered else "no native .so files"
    raise SystemExit(
        "APK has no supported game library. Expected one of: "
        "lib/x86/libcocos2dcpp.so, lib/x86/libgame.so, "
        "lib/armeabi/libgame.so, or lib/armeabi-v7a/libcocos2dcpp.so. "
        f"Found: {details}"
    )

if not exe.is_file():
    raise SystemExit(f"Selected {backend}, but the backend is not built: {exe}")

save = base / "save"
save.mkdir(parents=True, exist_ok=True)
print(f"Selected backend: {backend}")
print(f"Shared save folder: {save}")
print(f"GDPS server: {os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}")
print(f"Hack icons: {setting_bool('HACK_ICONS', False)}")
print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
print(f"Music pulse max: {os.environ.get('MUSIC_PULSE_MAX', '0.30')}")
print(f"Override ARM: {override_arm}")
raise SystemExit(subprocess.call(args, cwd=base))
