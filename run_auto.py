#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys, zipfile

root = Path(__file__).resolve().parent
base = root / "dist-unified" if (root / "dist-unified").is_dir() else root
apk = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else base / "game.apk"
if not apk.is_file():
    raise SystemExit(f"APK not found: {apk}")
with zipfile.ZipFile(apk) as z:
    names = set(z.namelist())
if "lib/x86/libcocos2dcpp.so" in names or "lib/x86/libgame.so" in names:
    exe = base / "x86" / "GeometryDashWrapper.exe"
    args = [str(exe), f"--apk={apk}"]
elif "lib/armeabi-v7a/libcocos2dcpp.so" in names:
    exe = base / "armv7" / "GeometryDashArmV7.exe"
    args = [str(exe), str(apk), "--companion-hooks=off", "--log=gd-armv7.log"]
elif "lib/armeabi/libcocos2dcpp.so" in names:
    exe = base / "arm-legacy" / "GeometryDashArmLegacy.exe"
    args = [str(exe), str(apk), "--log=gd-arm-legacy.log"]
else:
    raise SystemExit("APK has no supported x86, armeabi, or armeabi-v7a game library")
if not exe.is_file():
    raise SystemExit(f"Backend is not built: {exe}")
raise SystemExit(subprocess.call(args, cwd=exe.parent))
