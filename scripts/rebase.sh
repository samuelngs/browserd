#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

NEW_VERSION="${1:-}"
if [ -z "$NEW_VERSION" ]; then
    echo "Usage: scripts/rebase.sh <new-chromium-version>"
    echo "Example: scripts/rebase.sh 131.0.6778.69"
    exit 1
fi

OLD_VERSION="$(chromium_version)"
echo "==> Rebasing: $OLD_VERSION -> $NEW_VERSION"

echo "==> Step 1: Unapply current patches..."
"$SCRIPT_DIR/unapply.sh"

echo "==> Step 2: Update chromium_version.txt..."
echo "$NEW_VERSION" > "$ROOT_DIR/chromium_version.txt"

echo "==> Step 3: Fetch new version..."
"$SCRIPT_DIR/fetch.sh"

echo "==> Step 4: Apply patches to new version..."
if "$SCRIPT_DIR/apply.sh"; then
    echo ""
    echo "==> Rebase successful: $OLD_VERSION -> $NEW_VERSION"
    echo "==> Next: scripts/build.sh && scripts/verify.sh"
else
    echo ""
    echo "==> Rebase needs manual fixes."
    echo "==> Fix the failing patch, then:"
    echo "      git am --continue"
    echo "      scripts/export.sh   (re-export all patches)"
    exit 1
fi
