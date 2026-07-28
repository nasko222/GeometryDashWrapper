#!/usr/bin/env python3
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from datetime import datetime, timezone

LAUNCHER_VERSION = "0.9.5-unified7-fix2-focused+logging1"

root = Path(__file__).resolve().parent
base = root / "dist-unified" if (root / "dist-unified").is_dir() else root
apk = Path(sys.argv[1]).expanduser().resolve() if len(sys.argv) > 1 else base / "game.apk"
if not apk.is_file():
    raise SystemExit(f"APK not found: {apk}")
if apk.suffix.lower() != ".apk":
    raise SystemExit(f"The selected file is not an APK: {apk}")


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


def _decode_length8(data: bytes, offset: int) -> tuple[int, int]:
    first = data[offset]
    if first & 0x80:
        return ((first & 0x7F) << 8) | data[offset + 1], offset + 2
    return first, offset + 1


def _decode_length16(data: bytes, offset: int) -> tuple[int, int]:
    first = struct.unpack_from("<H", data, offset)[0]
    if first & 0x8000:
        second = struct.unpack_from("<H", data, offset + 2)[0]
        return ((first & 0x7FFF) << 16) | second, offset + 4
    return first, offset + 2


def _parse_string_pool(data: bytes, chunk_offset: int) -> list[str]:
    chunk_type, header_size, chunk_size = struct.unpack_from("<HHI", data, chunk_offset)
    if chunk_type != 0x0001 or header_size < 28 or chunk_offset + chunk_size > len(data):
        return []
    string_count, _style_count, flags, strings_start, _styles_start = struct.unpack_from(
        "<IIIII", data, chunk_offset + 8
    )
    offsets_base = chunk_offset + header_size
    data_base = chunk_offset + strings_start
    utf8 = bool(flags & 0x00000100)
    strings: list[str] = []
    for index in range(string_count):
        relative = struct.unpack_from("<I", data, offsets_base + index * 4)[0]
        position = data_base + relative
        try:
            if utf8:
                _utf16_length, position = _decode_length8(data, position)
                byte_length, position = _decode_length8(data, position)
                value = data[position:position + byte_length].decode("utf-8", errors="replace")
            else:
                char_length, position = _decode_length16(data, position)
                value = data[position:position + char_length * 2].decode("utf-16le", errors="replace")
        except (IndexError, struct.error):
            value = ""
        strings.append(value)
    return strings


def manifest_metadata(payload: bytes) -> tuple[str, str, str]:
    """Return package, versionName, versionCode from binary AndroidManifest.xml."""
    package = "unknown.package"
    version_name = "unknown"
    version_code = "unknown"
    try:
        xml_type, xml_header_size, xml_size = struct.unpack_from("<HHI", payload, 0)
        if xml_type != 0x0003 or xml_header_size < 8:
            raise ValueError("not binary Android XML")
        limit = min(xml_size, len(payload))
        offset = xml_header_size
        strings: list[str] = []
        while offset + 8 <= limit:
            chunk_type, header_size, chunk_size = struct.unpack_from("<HHI", payload, offset)
            if chunk_size < 8 or offset + chunk_size > limit:
                break
            if chunk_type == 0x0001:
                strings = _parse_string_pool(payload, offset)
            elif chunk_type == 0x0102 and strings and chunk_size >= 36:
                name_index = struct.unpack_from("<I", payload, offset + 20)[0]
                element_name = strings[name_index] if name_index < len(strings) else ""
                if element_name == "manifest":
                    attribute_start, attribute_size, attribute_count = struct.unpack_from(
                        "<HHH", payload, offset + 24
                    )
                    attributes_base = offset + 16 + attribute_start
                    for index in range(attribute_count):
                        item = attributes_base + index * attribute_size
                        if item + 20 > offset + chunk_size:
                            break
                        _ns, attr_name_index, raw_index = struct.unpack_from("<III", payload, item)
                        value_size, _zero, value_type, value_data = struct.unpack_from(
                            "<HBBI", payload, item + 12
                        )
                        if attr_name_index >= len(strings):
                            continue
                        attr_name = strings[attr_name_index]
                        raw_value = strings[raw_index] if raw_index != 0xFFFFFFFF and raw_index < len(strings) else ""
                        if raw_value:
                            value = raw_value
                        elif value_type == 0x03 and value_data < len(strings):
                            value = strings[value_data]
                        elif value_type in {0x10, 0x11}:
                            value = str(value_data)
                        elif value_type == 0x12:
                            value = "true" if value_data else "false"
                        else:
                            value = ""
                        if attr_name == "package" and value:
                            package = value
                        elif attr_name == "versionName" and value:
                            version_name = value
                        elif attr_name == "versionCode" and value:
                            version_code = value
                    break
            offset += chunk_size
    except (ValueError, IndexError, struct.error):
        pass

    # Fallback for unusual/plain-text manifests.
    texts = [payload.decode("utf-16le", errors="ignore"), payload.decode("utf-8", errors="ignore")]
    if package == "unknown.package":
        matches: list[str] = []
        for text in texts:
            matches.extend(re.findall(r"com\.[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+", text))
        preferred = [value for value in matches if "robtop" in value.lower() or "gdps" in value.lower()]
        values = preferred or matches
        if values:
            package = min(values, key=lambda value: (value.count("."), len(value)))
    if version_name == "unknown":
        for text in texts:
            match = re.search(r"versionName[^A-Za-z0-9._-]+([A-Za-z0-9._-]+)", text)
            if match:
                version_name = match.group(1)
                break
    return package, version_name, version_code


def game_identity(package: str, names: set[str]) -> tuple[str, str]:
    value = package.lower()
    basenames = {Path(name).name.lower() for name in names}
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


def safe_component(value: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", value.strip()).strip(".-_")
    return (cleaned or fallback)[:100]


def unique_directory(parent: Path, name: str) -> Path:
    candidate = parent / name
    if not candidate.exists():
        candidate.mkdir(parents=True)
        return candidate
    for index in range(2, 1000):
        candidate = parent / f"{name}__{index:02d}"
        if not candidate.exists():
            candidate.mkdir(parents=True)
            return candidate
    raise SystemExit("Could not create a unique log folder")


def matching_runtime_outputs(directory: Path) -> list[Path]:
    matches: list[Path] = []
    seen: set[Path] = set()
    for pattern in RUNTIME_OUTPUT_PATTERNS:
        for candidate in directory.glob(pattern):
            if candidate in seen or not candidate.is_file():
                continue
            seen.add(candidate)
            matches.append(candidate)
    return sorted(matches, key=lambda path: path.name.lower())


def move_with_unique_name(source: Path, destination_dir: Path) -> Path:
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / source.name
    if destination.exists():
        stem, suffix = source.stem, source.suffix
        for index in range(2, 1000):
            destination = destination_dir / f"{stem}__{index:02d}{suffix}"
            if not destination.exists():
                break
    try:
        return Path(shutil.move(str(source), str(destination)))
    except OSError as exc:
        raise SystemExit(
            f"Could not move runtime output {source}. A wrapper process may still be running: {exc}"
        ) from exc


with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    try:
        package, version_name, version_code = manifest_metadata(archive.read("AndroidManifest.xml"))
    except KeyError:
        package, version_name, version_code = "unknown.package", "unknown", "unknown"
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
elif has_legacy_arm:
    backend = "arm-legacy"
    exe = base / "arm-legacy" / "GeometryDashArmLegacy.exe"
elif has_armv7:
    backend = "armv7"
    exe = base / "armv7" / "GeometryDashArmV7.exe"
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

now = datetime.now().astimezone()
logs_root = base / "logs"
day_dir = logs_root / now.strftime("%Y-%m-%d")
folder_name = "__".join((
    now.strftime("%H-%M-%S"),
    safe_component(package, "unknown.package"),
    "v" + safe_component(version_name, "unknown"),
    backend,
))
run_dir = unique_directory(day_dir, folder_name)

# Preserve any root-level files left by an older launcher instead of deleting them.
old_outputs = matching_runtime_outputs(base)
if old_outputs:
    legacy_dir = unique_directory(
        logs_root / "_old-root-files",
        now.strftime("%Y-%m-%d_%H-%M-%S"),
    )
    for old_output in old_outputs:
        move_with_unique_name(old_output, legacy_dir)
    print(f"Archived old root logs: {legacy_dir}")

log_path = run_dir / (
    "gd-wrapper.log" if backend == "x86" else
    "gd-arm-legacy.log" if backend == "arm-legacy" else
    "gd-armv7.log"
)
profile_path = run_dir / "frame-profile.csv"
profile_summary_path = run_dir / "frame-profile-summary.txt"
imports_path = run_dir / "imports.txt"

if backend == "x86":
    # The existing x86 backend still writes its fixed filename in the wrapper root.
    # It is moved into this run folder immediately after the process exits.
    args = [str(exe), f"--apk={apk}"]
elif backend == "arm-legacy":
    args = [
        str(exe), str(apk),
        f"--log={log_path}",
        f"--profile={profile_path}",
        f"--profile-summary={profile_summary_path}",
    ]
else:
    args = [
        str(exe), str(apk), "--companion-hooks=shader",
        f"--log={log_path}",
        f"--profile={profile_path}",
        f"--profile-summary={profile_summary_path}",
        f"--dump-imports={imports_path}",
    ]

started_utc = datetime.now(timezone.utc)
run_info = run_dir / "run-info.txt"

def write_run_info(exit_code: int | None = None, error: str = "") -> None:
    lines = [
        f"launcher_version={LAUNCHER_VERSION}",
        f"started_local={now.isoformat()}",
        f"started_utc={started_utc.isoformat()}",
        f"backend={backend}",
        f"game_title={game_title}",
        f"package={package}",
        f"version_name={version_name}",
        f"version_code={version_code}",
        f"apk={apk}",
        f"apk_bytes={apk.stat().st_size}",
        f"log_folder={run_dir}",
        f"gdps_server={os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}",
        f"hack_icons={setting_bool('HACK_ICONS', False)}",
        f"full_bypass={setting_bool('FULL_BYPASS', True)}",
        f"force_highest_graphics={setting_bool('FORCE_HIGHEST_GRAPHICS', True)}",
        f"music_pulse_max={os.environ.get('MUSIC_PULSE_MAX', '0.30')}",
    ]
    if exit_code is not None:
        lines.extend((
            f"finished_utc={datetime.now(timezone.utc).isoformat()}",
            f"exit_code={exit_code}",
        ))
    if error:
        lines.append(f"launcher_error={error}")
    run_info.write_text("\n".join(lines) + "\n", encoding="utf-8")

write_run_info()
logs_root.mkdir(parents=True, exist_ok=True)
(logs_root / "latest-run.txt").write_text(str(run_dir) + "\n", encoding="utf-8")
readme = logs_root / "README.txt"
if not readme.exists():
    readme.write_text(
        "Each launch gets its own folder:\n"
        "logs\\YYYY-MM-DD\\HH-MM-SS__android.package__vVERSION__backend\\\n\n"
        "run-info.txt identifies the exact APK, package, version and backend.\n"
        "No previous run folder is deleted or overwritten.\n",
        encoding="utf-8",
    )

print(f"Game: {game_title}")
print(f"Package: {package}")
print(f"Version: {version_name} (code {version_code})")
print(f"Selected backend: {backend}")
print(f"Save root: {save_root}")
print(f"Log folder: {run_dir}")
print(f"GDPS server: {os.environ.get('GDPS_SERVER', 'www.boomlings.com/database')}")
print(f"Hack icons and colors: {setting_bool('HACK_ICONS', False)}")
print(f"Full bypass: {setting_bool('FULL_BYPASS', True)}")
print(f"Force highest graphics: {setting_bool('FORCE_HIGHEST_GRAPHICS', True)}")
print(f"Music pulse max: {os.environ.get('MUSIC_PULSE_MAX', '0.30')}")

exit_code = 1
launcher_error = ""
try:
    exit_code = subprocess.call(args, cwd=base, env=os.environ.copy())
except OSError as exc:
    launcher_error = str(exc)
    print(f"Could not launch backend: {exc}", file=sys.stderr)
finally:
    # Capture fixed-name or unexpected diagnostic files without changing backend behavior.
    for produced in matching_runtime_outputs(base):
        move_with_unique_name(produced, run_dir)
    write_run_info(exit_code, launcher_error)

raise SystemExit(exit_code)
