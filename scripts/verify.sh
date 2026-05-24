#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

BROWSERD_BIN="$SRC_DIR/out/Release/browserd"
FINGERPRINTS_DIR="$ROOT_DIR/fingerprints/current"

if [ ! -f "$BROWSERD_BIN" ]; then
    echo "ERROR: browserd binary not found at $BROWSERD_BIN"
    echo "Run scripts/build.sh first."
    exit 1
fi

mkdir -p "$FINGERPRINTS_DIR"

VERSION="$(chromium_version)"
REV="$(revision)"

echo "==> Fingerprint Verification Suite"
echo "==> Binary: $BROWSERD_BIN"
echo "==> Version: $VERSION-r$REV"
echo ""

echo "--- Manual Verification Targets ---"
echo ""
echo "  1. https://tls.peet.ws/api/all           (JA3, JA4, H2)"
echo "  2. https://browserleaks.com/javascript    (navigator props)"
echo "  3. https://browserleaks.com/webgl         (GPU fingerprint)"
echo "  4. https://bot.sannysoft.com              (bot detection battery)"
echo "  5. https://pixelscan.net                  (cross-OS coherence)"
echo "  6. https://abrahamjuliot.github.io/creepjs (aggressive fingerprint)"
echo ""
echo "Compare each against retail Chrome on same OS/version."
echo "Save results to: fingerprints/current/"
echo "Diff against:    fingerprints/control/"
echo ""

if [ -d "$ROOT_DIR/fingerprints/control" ] && [ "$(ls -A "$ROOT_DIR/fingerprints/control" 2>/dev/null)" ]; then
    echo "--- Diffing against control fingerprints ---"
    PASS=0
    FAIL=0
    for control_file in "$ROOT_DIR/fingerprints/control"/*.json; do
        basename="$(basename "$control_file")"
        current_file="$FINGERPRINTS_DIR/$basename"
        if [ -f "$current_file" ]; then
            if diff -q "$control_file" "$current_file" > /dev/null 2>&1; then
                echo "  MATCH: $basename"
                PASS=$((PASS + 1))
            else
                echo "  DELTA: $basename <-- investigate"
                diff --color=auto "$control_file" "$current_file" || true
                FAIL=$((FAIL + 1))
            fi
        else
            echo "  MISSING: $basename (run fingerprint capture first)"
        fi
    done
    echo ""
    echo "==> Matched: $PASS, Deltas: $FAIL"
else
    echo "No control fingerprints found. Capture retail Chrome fingerprints first:"
    echo "  Save JSON to fingerprints/control/"
fi
