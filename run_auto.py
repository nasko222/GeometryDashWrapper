#!/usr/bin/env python3
from pathlib import Path
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

# Explicit priority order: native x86 first, then legacy ARM, then ARMv7/2.2.
if "lib/x86/libcocos2dcpp.so" in names or "lib/x86/libgame.so" in names:
    backend = "x86"
    exe = base / "x86" / "GeometryDashWrapper.exe"
    args = [str(exe), f"--apk={apk}"]
elif "lib/armeabi/libgame.so" in names:
    backend = "arm-legacy"
    exe = base / "arm-legacy" / "GeometryDashArmLegacy.exe"
    args = [str(exe), str(apk), "--log=gd-arm-legacy.log"]
elif "lib/armeabi-v7a/libcocos2dcpp.so" in names:
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
raise SystemExit(subprocess.call(args, cwd=base))
