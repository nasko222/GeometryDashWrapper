#!/usr/bin/env bash
set -euo pipefail
zig="${ZIG:-$(command -v zig || true)}"
if [[ -z "$zig" ]]; then
    echo "Set ZIG to the Zig executable" >&2
    exit 2
fi
cache="${GD_ARM_BUILD_CACHE:-$PWD/.zig-cache}"
export ZIG_GLOBAL_CACHE_DIR="$cache/global"
export ZIG_LOCAL_CACHE_DIR="$cache/local"
args=()
for arg in "$@"; do
    case "$arg" in
        -mthreads|-static-libgcc) ;;
        *) args+=("$arg") ;;
    esac
done
exec "$zig" cc -target x86-windows-gnu "${args[@]}"
