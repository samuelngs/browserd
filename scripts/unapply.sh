#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
require_src

VERSION="$(chromium_version)"

cd "$SRC_DIR"

echo "==> Resetting to clean tag $VERSION..."
git checkout "browserd-patches"
git reset --hard "refs/tags/$VERSION"

echo "==> Cleaning patch refs..."
git for-each-ref --format='%(refname)' refs/patches/ | while read -r ref; do
    git update-ref -d "$ref"
done

echo "==> All patches removed. Source is clean at $VERSION."
