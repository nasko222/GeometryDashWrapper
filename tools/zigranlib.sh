#!/usr/bin/env bash
set -euo pipefail
zig="${ZIG:-$(command -v zig || true)}"
[[ -n "$zig" ]] || { echo "Set ZIG to the Zig executable" >&2; exit 2; }
exec "$zig" ranlib "$@"
