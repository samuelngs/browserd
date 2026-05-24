#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

VERSION="$(chromium_version)"
GCLIENT_DIR="$ROOT_DIR/chromium"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)}"

# --- Step 1: Ensure Chromium source exists ---

if [ ! -d "$SRC_DIR/.git" ]; then
    echo "==> Chromium source not found. Fetching $VERSION..."
    "$SCRIPT_DIR/fetch.sh"
fi

# --- Step 2: Ensure depot_tools + gclient state ---

ensure_depot_tools

if [ ! -f "$GCLIENT_DIR/.gclient_entries" ]; then
    echo "==> Running gclient sync..."
    cd "$GCLIENT_DIR"
    gclient sync -D --no-history
fi

# --- Step 3: Ensure browserd symlink ---

if [ ! -L "$SRC_DIR/browserd" ]; then
    ln -sfn ../../browserd "$SRC_DIR/browserd"
fi

# --- Step 4: Apply patches if needed ---

upstream_head=$(cd "$SRC_DIR" && git rev-parse "refs/patches/upstream-head" 2>/dev/null || echo "")
if [ -z "$upstream_head" ]; then
    echo "==> Applying patches..."
    "$SCRIPT_DIR/apply.sh"
fi

# --- Step 5: Build ---

cd "$SRC_DIR"

mkdir -p out/Release
cp "$ROOT_DIR/flags.gn" out/Release/args.gn

echo "==> Running gn gen..."
gn gen out/Release

echo "==> Building browserd with $JOBS jobs..."
autoninja -C out/Release browserd -j "$JOBS"

# --- Step 6: Link output to repo root ---

ln -sfn chromium/src/out/Release "$ROOT_DIR/out"

echo "==> Build complete: out/browserd"
