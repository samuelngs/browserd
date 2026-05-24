#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
require_src

TARGET_GROUP="${1:-}"

cd "$SRC_DIR"

if ! git rev-parse "$(patch_ref upstream-head)" > /dev/null 2>&1; then
    echo "ERROR: No upstream-head ref found."
    echo "Patches must be applied via scripts/apply.sh first."
    exit 1
fi

export_group() {
    local patch_dir="$1"
    local repo="$2"
    local desc="$3"
    local group_name
    group_name="$(patch_group_name "$patch_dir")"

    if [ -n "$TARGET_GROUP" ] && [ "$group_name" != "$TARGET_GROUP" ]; then
        return
    fi

    local start_ref
    start_ref="$(get_start_ref "$group_name")"
    local end_ref
    end_ref="$(patch_ref "$group_name")"

    if ! git rev-parse "$end_ref" > /dev/null 2>&1; then
        echo "==> [$group_name] No end ref found. Skipping."
        return
    fi

    local start_sha end_sha
    start_sha="$(git rev-parse "$start_ref")"
    end_sha="$(git rev-parse "$end_ref")"

    if [ "$start_sha" = "$end_sha" ]; then
        echo "==> [$group_name] No patches in range. Clearing."
        : > "$ROOT_DIR/$patch_dir/.patches"
        rm -f "$ROOT_DIR/$patch_dir/"*.patch
        return
    fi

    echo "==> [$group_name] Exporting patches..."

    local tmpdir
    tmpdir="$(mktemp -d)"
    git format-patch \
        --zero-commit \
        --full-index \
        --no-signature \
        --output-directory "$tmpdir" \
        "$start_ref..$end_ref"

    rm -f "$ROOT_DIR/$patch_dir/"*.patch
    : > "$ROOT_DIR/$patch_dir/.patches"

    local count=0
    for raw_patch in "$tmpdir"/*.patch; do
        [ -f "$raw_patch" ] || continue

        count=$((count + 1))
        local subject
        subject="$(sed -n 's/^Subject: \[PATCH[^]]*\] //p' "$raw_patch" | head -1)"
        local basename
        basename="$(derive_patch_filename "$subject")"
        local filename
        filename="$(printf '%04d_' "$count")$basename"

        cp "$raw_patch" "$ROOT_DIR/$patch_dir/$filename"
        echo "$filename" >> "$ROOT_DIR/$patch_dir/.patches"
    done

    rm -rf "$tmpdir"
    echo "==> [$group_name] Exported $count patches."
}

# Determine start ref for a group based on config.json order.
# First group starts at upstream-head, subsequent groups start at previous group's end ref.
get_start_ref() {
    local target_group="$1"
    local prev_ref
    prev_ref="$(patch_ref upstream-head)"

    local count
    count=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(len(d))")
    for i in $(seq 0 $((count - 1))); do
        local patch_dir group_name
        patch_dir=$(python3 -c "import json; d=json.load(open('$CONFIG_FILE')); print(d[$i]['patch_dir'])")
        group_name="$(patch_group_name "$patch_dir")"

        if [ "$group_name" = "$target_group" ]; then
            echo "$prev_ref"
            return
        fi
        prev_ref="$(patch_ref "$group_name")"
    done

    echo "$(patch_ref upstream-head)"
}

each_patch_group export_group

echo ""
echo "==> Export complete. Review changes in patches/ and commit."
