#!/usr/bin/env bash
# rebuild_vendor_boot.sh
#
# Rebuilds vendor_boot.img by replacing ramdisk .ko files with CI-built ones.
# Ramdisk: LZ4-compressed cpio
#
# Usage: rebuild_vendor_boot.sh <built_modules_dir> <output_img>
#   built_modules_dir  directory containing CI-built .ko files (searched recursively)
#   output_img         path for the rebuilt vendor_boot.img

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

STOCK_IMG="${REPO_ROOT}/stock_images/vendor_boot.img"
BUILT_MODULES_DIR="${1:?Usage: $0 <built_modules_dir> <output_img>}"
OUTPUT_IMG="${2:?Usage: $0 <built_modules_dir> <output_img>}"

UNPACK_BOOTIMG="$REPO_ROOT/prebuilts_a06x/mkbootimg/unpack_bootimg.py"
MKBOOTIMG="$REPO_ROOT/prebuilts_a06x/mkbootimg/mkbootimg.py"

WORK_DIR="$(mktemp -d)"
trap "rm -rf $WORK_DIR" EXIT

echo "[INFO] Unpacking stock vendor_boot.img..."
python3 "$UNPACK_BOOTIMG" \
    --boot_img "$STOCK_IMG" \
    --out "$WORK_DIR/unpacked" \
    --format mkbootimg > "$WORK_DIR/mkbootimg_args.txt" 2>/dev/null

echo "[INFO] Extracting ramdisk..."
mkdir -p "$WORK_DIR/ramdisk"
lz4 -d "$WORK_DIR/unpacked/vendor_ramdisk00" "$WORK_DIR/ramdisk.cpio"
cd "$WORK_DIR/ramdisk"
cpio -idm < "$WORK_DIR/ramdisk.cpio"
cd "$REPO_ROOT"

MODULES_DIR="$WORK_DIR/ramdisk/lib/modules"
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

echo "[INFO] Repacking ramdisk..."
cd "$WORK_DIR/ramdisk"
find . | cpio -H newc -o > "$WORK_DIR/ramdisk_new.cpio" 2>/dev/null
lz4 -l -9 "$WORK_DIR/ramdisk_new.cpio" "$WORK_DIR/vendor_ramdisk00_new"
cd "$REPO_ROOT"

# Replace ramdisk fragment path in mkbootimg args
sed -i "s|$WORK_DIR/unpacked/vendor_ramdisk00|$WORK_DIR/vendor_ramdisk00_new|g" \
    "$WORK_DIR/mkbootimg_args.txt"

echo "[INFO] Repacking vendor_boot.img..."
mkdir -p "$(dirname "$OUTPUT_IMG")"
eval python3 "$MKBOOTIMG" \
    --vendor_boot "$OUTPUT_IMG" \
    $(cat "$WORK_DIR/mkbootimg_args.txt")

echo "[INFO] Done: $OUTPUT_IMG ($(du -h "$OUTPUT_IMG" | cut -f1))"
