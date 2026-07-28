#!/usr/bin/env python3
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import zipfile
from datetime import datetime, timezone

root = Path(__file__).resolve().parent
base = root / "dist-unified" if (root / "dist-unified").is_dir() else root
apk = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else base / "game.apk"
if not apk.is_file():
    raise SystemExit(f"APK not found: {apk}")


RUNTIME_OUTPUT_PATTERNS = (
    "gd-wrapper*.log",
    "gd-arm*.log",
    "gd-dynarmic*.log",
    "gd-networktest*.log",
    "gd-v22beta*.log",
    "gd-arm*-imports*.txt",
    "gd-v22beta-imports*.txt",
    "gd-arm*-profile*.csv",
    "gd-dynarmic-profile*.csv",
    "gd-networktest*-profile*.csv",
    "gd-arm*-profile-summary*.txt",
    "gd-dynarmic-profile-summary*.txt",
    "gd-networktest*-profile-summary*.txt",
    "gd-run-info.txt",
)


def clear_previous_runtime_outputs(directory: Path) -> list[str]:
    removed: list[str] = []
    failures: list[str] = []
    seen: set[Path] = set()
    for pattern in RUNTIME_OUTPUT_PATTERNS:
        for candidate in directory.glob(pattern):
            if candidate in seen or not candidate.is_file():
                continue
            seen.add(candidate)
            try:
                candidate.unlink()
                removed.append(candidate.name)
            except OSError as exc:
                failures.append(f"{candidate.name}: {exc}")
    if failures:
        details = "\n  ".join(failures)
        raise SystemExit(
            "Could not delete previous runtime logs. A wrapper process may still be "
            f"running. Close it and retry:\n  {details}"
        )
    return sorted(removed)


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
    ordered = sorted(unique.items(), key=lambda item: (item[0][0] * item[0][1], item[0]))
    offset = 6 + 16 * len(ordered)
    entries: list[bytes] = []
    payloads: list[bytes] = []
    for (width, height), payload in ordered:
        entries.append(struct.pack(
            "<BBBBHHII",
            0 if width >= 256 else width,
            0 if height >= 256 else height,
            0, 0, 1, 32, len(payload), offset,
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
        if not lower.endswith(".png"):
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
        if lower.startswith("res/"): score += 12000
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


def game_identity(package: str, names: set[str]) -> tuple[str, str]:
    value = package.lower()
    basenames = {Path(name).name.lower() for name in names}

    # Official and GDPS package names are the strongest signal. Full Geometry
    # Dash APKs can also bundle World/Meltdown level-data files, so those asset
    # names must not override an explicit geometryjump package.
    if "subzero" in value:
        return "Geometry Dash SubZero", "geometry-dash-subzero"
    if "meltdown" in value and "world" not in value:
        return "Geometry Dash Meltdown", "geometry-dash-meltdown"
    if "world" in value:
        return "Geometry Dash World", "geometry-dash-world"
    if "lite" in value:
        return "Geometry Dash Lite", "geometry-dash-lite"
    if value == "unknown.package" or "geometryjump" not in value:
        if {"pressstart.mp3", "nockem.mp3", "powertrip.mp3"}.issubset(basenames):
            return "Geometry Dash SubZero", "geometry-dash-subzero"
        if {"thesevenseas.mp3", "vikingarena.mp3", "airbornerobots.mp3"}.issubset(basenames):
            return "Geometry Dash Meltdown", "geometry-dash-meltdown"
        if {"payload.mp3", "beastmode.mp3"}.issubset(basenames):
            return "Geometry Dash World", "geometry-dash-world"
    return "Geometry Dash", "geometry-dash"


with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    try:
        package = manifest_package(archive.read("AndroidManifest.xml"))
    except KeyError:
        package = "unknown.package"
    game_title, icon_slug = game_identity(package, names)

    icon_source = ""
    icon_path: Path | None = None
    for override in (base / "icon.ico", base / "icon.png", root / "icon.ico", root / "icon.png"):
        if not override.is_file():
            continue
        if override.suffix.lower() == ".ico":
            icon_path = override
            icon_source = str(override)
            break
        candidate = override.read_bytes()
        if png_size(candidate):
            generated = base / "save" / "wrapper-icon.ico"
            if write_png_ico([candidate], generated):
                icon_path = generated
                icon_source = str(override)
                break

    if icon_path is None:
        bundled = base / "assets" / "icons" / f"{icon_slug}.ico"
        if not bundled.is_file():
            bundled = root / "assets" / "icons" / f"{icon_slug}.ico"
        if bundled.is_file():
            icon_path = bundled
            icon_source = str(bundled)

    if icon_path is None:
        selected_icon = choose_apk_icons(archive)
        if selected_icon:
            source, payloads = selected_icon
            generated = base / "save" / "wrapper-icon.ico"
            if write_png_ico(payloads, generated):
                icon_path = generated
                icon_source = f"APK:{source}"

save_root = base / "save"
save_root.mkdir(parents=True, exist_ok=True)
os.environ["GD_SAVE_DIR"] = str(save_root.resolve())
os.environ["GD_GAME_TITLE"] = game_title
(save_root / ".last-package").write_text(package + "\n", encoding="utf-8")

if icon_path is not None:
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
    args = [str(exe), str(apk), "--companion-hooks=shader", "--log=gd-armv7.log"]
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

removed_outputs = clear_previous_runtime_outputs(base)
if removed_outputs:
    print("Deleted previous runtime outputs: " + ", ".join(removed_outputs))
else:
    print("Deleted previous runtime outputs: none found")

run_info = base / "gd-run-info.txt"
run_info.write_text(
    "\n".join((
        "Geometry Dash Wrapper unified7-fix2 run marker",
        f"started_utc={datetime.now(timezone.utc).isoformat()}",
        f"backend={backend}",
        f"game_title={game_title}",
        f"package={package}",
        f"apk={apk}",
        f"apk_bytes={apk.stat().st_size}",
    )) + "\n",
    encoding="utf-8",
)

print(f"Game: {game_title}")
print(f"Package: {package}")
print(f"Selected backend: {backend}")
print(f"Save root: {save_root}")
print(f"GDPS server: {os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}")
print(f"Hack icons and colors: {setting_bool('HACK_ICONS', False)}")
print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
print(f"Force highest graphics: {setting_bool('FORCE_HIGHEST_GRAPHICS', True)}")
print(f"Music pulse max: {os.environ.get('MUSIC_PULSE_MAX', '0.30')}")
raise SystemExit(subprocess.call(args, cwd=base, env=os.environ.copy()))
