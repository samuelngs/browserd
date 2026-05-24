#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
require_src

cd "$SRC_DIR"

echo "==> Saving upstream-head ref..."
git update-ref "$(patch_ref upstream-head)" HEAD

TOTAL_APPLIED=0
TOTAL_FAILED=0

apply_group() {
    local patch_dir="$1"
    local repo="$2"
    local desc="$3"
    local group_name
    group_name="$(patch_group_name "$patch_dir")"
    local patches_file="$ROOT_DIR/$patch_dir/.patches"

    if [ ! -f "$patches_file" ] || [ ! -s "$patches_file" ]; then
        echo "==> [$group_name] No patches. Skipping."
        git update-ref "$(patch_ref "$group_name")" HEAD
        return
    fi

    echo "==> [$group_name] Applying patches ($desc)..."
    local applied=0
    local failed=0

    while IFS= read -r line; do
        line="$(echo "$line" | sed 's/#.*//' | xargs)"
        [ -z "$line" ] && continue

        local patch_file="$ROOT_DIR/$patch_dir/$line"
        if [ ! -f "$patch_file" ]; then
            echo "    SKIP: $line (file not found)"
            continue
        fi

        echo "    Applying: $line"
        if git am --3way "$patch_file" 2>/dev/null; then
            applied=$((applied + 1))
        elif git apply --3way "$patch_file" 2>/dev/null; then
            git add -A
            git commit -m "patch: $line"
            applied=$((applied + 1))
        else
            echo "    FAIL: $line"
            echo "    Resolve manually, then: git am --continue"
            failed=$((failed + 1))
            TOTAL_FAILED=$((TOTAL_FAILED + failed))
            return
        fi
    done < "$patches_file"

    git update-ref "$(patch_ref "$group_name")" HEAD
    echo "==> [$group_name] Applied: $applied"
    TOTAL_APPLIED=$((TOTAL_APPLIED + applied))
}

each_patch_group apply_group

echo ""
echo "==> Total applied: $TOTAL_APPLIED, Failed: $TOTAL_FAILED"
[ "$TOTAL_FAILED" -eq 0 ] || exit 1
