#!/usr/bin/env bash
# rebuild_vendor_dlkm.sh
#
# Rebuilds vendor_dlkm.img by replacing stock .ko files with CI-built ones.
# Filesystem: erofs
#
# Usage: rebuild_vendor_dlkm.sh <built_modules_dir> <output_img>
#   built_modules_dir  directory containing CI-built .ko files (searched recursively)
#   output_img         path for the rebuilt vendor_dlkm.img

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

STOCK_IMG="${REPO_ROOT}/stock_images/vendor_dlkm.img"
BUILT_MODULES_DIR="${1:?Usage: $0 <built_modules_dir> <output_img>}"
OUTPUT_IMG="${2:?Usage: $0 <built_modules_dir> <output_img>}"

# Timestamp from stock image (keep identical to stock for reproducibility)
STOCK_TIMESTAMP=1757501825

WORK_DIR="$(mktemp -d)"
trap "rm -rf $WORK_DIR" EXIT

echo "[INFO] Extracting stock vendor_dlkm.img (erofs)..."
fsck.erofs --extract="$WORK_DIR/extracted" "$STOCK_IMG"

MODULES_DIR="$WORK_DIR/extracted/lib/modules"
REPLACED=0
KEPT=0

echo "[INFO] Replacing modules with CI-built versions..."
for stock_ko in "$MODULES_DIR"/*.ko; do
    name="$(basename "$stock_ko")"
    built_ko="$(find "$BUILT_MODULES_DIR" -name "$name" 2>/dev/null | head -1)"
    if [[ -n "$built_ko" ]]; then
        cp "$built_ko" "$stock_ko"
        REPLACED=$((REPLACED + 1))
        echo "  [REPLACED] $name"
    else
        KEPT=$((KEPT + 1))
    fi
done

echo "[INFO] Replaced: $REPLACED | Kept stock: $KEPT"

echo "[INFO] Repacking as erofs..."
mkdir -p "$(dirname "$OUTPUT_IMG")"
mkfs.erofs \
    -z lz4 \
    -T $STOCK_TIMESTAMP \
    "$OUTPUT_IMG" \
    "$WORK_DIR/extracted"

echo "[INFO] Done: $OUTPUT_IMG ($(du -h "$OUTPUT_IMG" | cut -f1))"
