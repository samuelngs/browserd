#!/usr/bin/env bash
set -euo pipefail

CHROMIUM_REPO="https://chromium.googlesource.com/chromium/src.git"

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

ensure_depot_tools

VERSION="$(chromium_version)"
GCLIENT_DIR="$ROOT_DIR/chromium"

echo "==> Chromium version: $VERSION"

if [ ! -d "$SRC_DIR/.git" ]; then
    echo "==> Cloning chromium (shallow)..."
    mkdir -p "$GCLIENT_DIR"
    cd "$GCLIENT_DIR"
    git clone --depth 1 --branch "$VERSION" "$CHROMIUM_REPO" src
else
    echo "==> Source exists. Fetching tag $VERSION..."
    cd "$SRC_DIR"
    git fetch --depth 1 origin "refs/tags/$VERSION:refs/tags/$VERSION"
    git checkout "refs/tags/$VERSION"
fi

cd "$SRC_DIR"

echo "==> Creating patch branch..."
git checkout -B browserd-patches "refs/tags/$VERSION"

echo "==> Running gclient sync..."
cd "$GCLIENT_DIR"
gclient sync -D --no-history

echo "==> Creating browserd symlink..."
ln -sfn ../../browserd "$SRC_DIR/browserd"

echo "==> Copying flags.gn..."
mkdir -p "$SRC_DIR/out/Release"
cp "$ROOT_DIR/flags.gn" "$SRC_DIR/out/Release/args.gn"

echo "==> Done. Run scripts/apply.sh to apply patches."
