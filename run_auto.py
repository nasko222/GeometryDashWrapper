#!/usr/bin/env python3
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

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


def write_png_ico(images: list[bytes], destination: Path) -> bool:
    unique: dict[tuple[int, int], bytes] = {}
    for payload in images:
        size = png_size(payload)
        if size:
            unique[size] = payload
    if not unique:
        return False
    # Windows chooses the closest entry for the title bar/taskbar. Keep a range
    # of real APK density images instead of one large PNG blurred down to 16px.
    ordered = sorted(unique.items(), key=lambda item: (item[0][0] * item[0][1], item[0]))
    header_size = 6 + 16 * len(ordered)
    offset = header_size
    entries = []
    payloads = []
    for (width, height), payload in ordered:
        entry_width = 0 if width >= 256 else width
        entry_height = 0 if height >= 256 else height
        entries.append(struct.pack(
            "<BBBBHHII", entry_width, entry_height, 0, 0, 1, 32,
            len(payload), offset,
        ))
        payloads.append(payload)
        offset += len(payload)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(
        struct.pack("<HHH", 0, 1, len(ordered)) + b"".join(entries) + b"".join(payloads)
    )
    return True


def choose_apk_icons(archive: zipfile.ZipFile) -> tuple[str, list[bytes]] | None:
    rejected = (
        "notification", "notify", "ad_", "ads", "google", "facebook",
        "twitter", "amazon", "exo_", "mbridge", "privacy", "signin",
        "play_", "billing", "achievement", "everyplay",
    )
    candidates: list[tuple[int, str, bytes, tuple[int, int]]] = []
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
        if basename == "icon.png": score += 16000
        if basename.startswith("ic_launcher"): score += 20000
        if is_mipmap: score += 3000
        if abs(width - height) <= max(width, height) // 8: score += 1000
        candidates.append((score, name, payload, size))
    if not candidates:
        return None
    best = max(candidates, key=lambda item: (item[0], len(item[2])))
    best_base = Path(best[1]).name
    family = [item for item in candidates if Path(item[1]).name == best_base]
    if len({item[3] for item in family}) < 2:
        family = sorted(candidates, key=lambda item: item[0], reverse=True)[:12]
    return best[1], [item[2] for item in family]


def manifest_package(payload: bytes) -> str:
    texts = [payload.decode("utf-16le", errors="ignore"), payload.decode("utf-8", errors="ignore")]
    matches: list[str] = []
    for text in texts:
        matches.extend(re.findall(r"com\.[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+", text))
    preferred = [value for value in matches if "robtop" in value.lower() or "gdps" in value.lower()]
    values = preferred or matches
    if not values:
        return "unknown.package"
    return min(values, key=lambda value: (value.count("."), len(value)))


def game_title_for(package: str, names: set[str]) -> str:
    value = package.lower()
    if "subzero" in value or any("leveldatasubzero" in name.lower() for name in names):
        return "Geometry Dash SubZero"
    if "meltdown" in value and "world" not in value:
        return "Geometry Dash Meltdown"
    if "world" in value:
        return "Geometry Dash World"
    return "Geometry Dash"


def apk_fingerprint(path: Path) -> str:
    checksum = 0
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            checksum = zlib.crc32(block, checksum)
    return f"{checksum & 0xffffffff:08x}-{path.stat().st_size}"


def migrate_matching_flat_save(save_root: Path, profile: Path, package: str, fingerprint: str) -> None:
    game_files = [entry for entry in save_root.glob("CC*.dat*") if entry.is_file()]
    if not game_files or any(profile.glob("CC*.dat*")):
        return
    cache_root = save_root / "apk-member-cache"
    current_cache_seen = (cache_root / fingerprint).is_dir()
    no_apk_cache_history = not cache_root.exists() or not any(cache_root.iterdir())
    # A matching cache proves which APK produced the old flat files. A clean
    # x86-only installation has no member-cache history and is also safe.
    if not current_cache_seen and not no_apk_cache_history:
        return
    profile.mkdir(parents=True, exist_ok=True)
    for source in game_files:
        shutil.copy2(source, profile / source.name)
    preferences = save_root / "preferences.bin"
    if preferences.is_file() and not (profile / preferences.name).exists():
        shutil.copy2(preferences, profile / preferences.name)
    print(f"Migrated compatible flat saves into profile: {package}")


with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    try:
        package = manifest_package(archive.read("AndroidManifest.xml"))
    except KeyError:
        package = "unknown.package"
    game_title = game_title_for(package, names)
    icon_source = ""
    icon_payloads: list[bytes] = []
    for override in (base / "icon.png", root / "icon.png"):
        if override.is_file():
            candidate = override.read_bytes()
            if png_size(candidate):
                icon_source = str(override)
                icon_payloads = [candidate]
                break
    if not icon_payloads:
        selected_icon = choose_apk_icons(archive)
        if selected_icon:
            icon_source, icon_payloads = selected_icon

save_root = base / "save"
profile_name = re.sub(r"[^A-Za-z0-9._-]+", "_", package) or "unknown.package"
profile_save = save_root / "profiles" / profile_name
save_root.mkdir(parents=True, exist_ok=True)
profile_save.mkdir(parents=True, exist_ok=True)
migrate_matching_flat_save(save_root, profile_save, package, apk_fingerprint(apk))
os.environ["GD_SAVE_DIR"] = str(profile_save.resolve())
os.environ["GD_GAME_TITLE"] = game_title
(save_root / ".last-package").write_text(package + "\n", encoding="utf-8")

if icon_payloads:
    icon_path = save_root / "wrapper-icon.ico"
    if write_png_ico(icon_payloads, icon_path):
        os.environ["GD_WINDOW_ICON"] = str(icon_path.resolve())
        print(f"Window icon: {icon_source}")

has_x86 = "lib/x86/libcocos2dcpp.so" in names or "lib/x86/libgame.so" in names
has_legacy_arm = "lib/armeabi/libgame.so" in names
has_armv7 = "lib/armeabi-v7a/libcocos2dcpp.so" in names
if has_x86:
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

print(f"Game: {game_title}")
print(f"Package: {package}")
print(f"Selected backend: {backend}")
print(f"Save root: {save_root}")
print(f"Active save profile: {profile_save}")
print(f"GDPS server: {os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}")
print(f"Hack icons and colors: {setting_bool('HACK_ICONS', False)}")
print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
print(f"Force highest graphics: {setting_bool('FORCE_HIGHEST_GRAPHICS', True)}")
print(f"Music pulse max: {os.environ.get('MUSIC_PULSE_MAX', '0.30')}")
raise SystemExit(subprocess.call(args, cwd=base, env=os.environ.copy()))
