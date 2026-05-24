#!/usr/bin/env bash
# Shared helpers for browserd scripts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$ROOT_DIR/chromium/src"
PATCHES_DIR="$ROOT_DIR/patches"
CONFIG_FILE="$PATCHES_DIR/config.json"

DEPOT_TOOLS_REPO="https://chromium.googlesource.com/chromium/tools/depot_tools.git"
DEPOT_TOOLS_BOOTSTRAP="$ROOT_DIR/chromium/depot_tools"

if [ -d "$DEPOT_TOOLS_BOOTSTRAP" ]; then
    export PATH="$DEPOT_TOOLS_BOOTSTRAP:$PATH"
elif [ -d "$SRC_DIR/third_party/depot_tools" ]; then
    export PATH="$SRC_DIR/third_party/depot_tools:$PATH"
fi

ensure_depot_tools() {
    if [ ! -d "$DEPOT_TOOLS_BOOTSTRAP" ]; then
        echo "==> depot_tools not found. Downloading..."
        mkdir -p "$ROOT_DIR/chromium"
        git clone "$DEPOT_TOOLS_REPO" "$DEPOT_TOOLS_BOOTSTRAP"
        export PATH="$DEPOT_TOOLS_BOOTSTRAP:$PATH"
    fi
    if [ -d "$DEPOT_TOOLS_BOOTSTRAP" ] && [ ! -f "$DEPOT_TOOLS_BOOTSTRAP/python3_bin_reldir.txt" ]; then
        echo "==> Bootstrapping depot_tools..."
        "$DEPOT_TOOLS_BOOTSTRAP/ensure_bootstrap"
    fi
}

chromium_version() {
    cat "$ROOT_DIR/chromium_version.txt" | tr -d '[:space:]'
}

revision() {
    cat "$ROOT_DIR/revision.txt" | tr -d '[:space:]'
}

require_src() {
    if [ ! -d "$SRC_DIR/.git" ]; then
        echo "ERROR: Chromium source not found at $SRC_DIR"
        echo "Run scripts/fetch.sh first."
        exit 1
    fi
}

# Read config.json entries. Calls callback with: patch_dir repo description
# Usage: each_patch_group callback_fn
each_patch_group() {
    local callback="$1"
    local count
    count=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(len(d))")
    for i in $(seq 0 $((count - 1))); do
        local patch_dir repo desc
        patch_dir=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(d[$i]['patch_dir'])")
        repo=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(d[$i]['repo'])")
        desc=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(d[$i].get('description',''))")
        $callback "$patch_dir" "$repo" "$desc"
    done
}

# Derive patch filename from git commit subject.
# "webdriver: return false unconditionally" → "webdriver_return_false_unconditionally.patch"
derive_patch_filename() {
    local subject="$1"
    echo "$subject" \
        | tr '[:upper:]' '[:lower:]' \
        | sed 's/[^a-z0-9]/_/g' \
        | sed 's/__*/_/g' \
        | sed 's/^_//;s/_$//' \
        | cut -c1-80 \
        | sed 's/$/.patch/'
}

# Get the ref name for a patch group boundary.
# Before any patches: refs/patches/upstream-head
# After a group:      refs/patches/<dirname>
patch_ref() {
    local name="$1"
    echo "refs/patches/$name"
}

# Get dirname from patch_dir (e.g., "patches/core" → "core")
patch_group_name() {
    basename "$1"
}
